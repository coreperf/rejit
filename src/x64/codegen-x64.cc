// Copyright (C) 2013 Alexandre Rames <alexandre@uop.re>
// rejit is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "codegen.h"
#include "x64/macro-assembler-x64.h"
#include <cmath>


namespace rejit {
namespace internal {


#define __ masm_->

Codegen::Codegen()
  : masm_(new MacroAssembler()),
    ring_base_(rax, 0) {}


const int kStateInfoSize = 7 * kPointerSize;


int Codegen::TimeSummaryBaseOffsetFromFrame() {
  return -kStateInfoSize - time_summary_size();
}


Operand Codegen::TimeSummary(int offset) {
  return Operand(rbp, TimeSummaryBaseOffsetFromFrame() + offset);
}


Operand Codegen::TimeSummaryOperand(int time) {
  return Operand(rbp, TimeSummaryBaseOffsetFromFrame() + time / kBitsPerByte);
}


Operand Codegen::StateRingBase() {
  return Operand(rbp, StateRingBaseOffsetFromFrame());
}


int Codegen::StateRingBaseOffsetFromFrame() {
  return TimeSummaryBaseOffsetFromFrame() - state_ring_size();
}


Operand Codegen::StateOperand(Register offset) {
  return Operand(rbp, offset, times_1, StateRingBaseOffsetFromFrame());
}


Operand Codegen::StateOperand(int time, int state_index) {
  return Operand(rbp,
                 ring_index,
                 times_1,
                 StateRingBaseOffsetFromFrame() +
                 time * state_ring_time_size() + state_index * kPointerSize);
}


Operand Codegen::StateOperand(int time, Register state_index) {
  __ movq(scratch, state_index);
  __ shl(scratch, Immediate(kPointerSizeLog2));
  __ addq(scratch, ring_index);
  __ addq(scratch, Immediate(StateRingBaseOffsetFromFrame() + time *
                             state_ring_time_size()));
  return Operand(rbp,
                 scratch,
                 times_1,
                 0);
}


void Codegen::ComputeStateOperandOffset(Register offset, int time, int index) {
  ASSERT(!offset.is(scratch1));
  __ Move(scratch1, 0);
  __ Move(offset,
       time * state_ring_time_size() + index * kPointerSize);
  __ addq(offset, ring_index);

  __ cmpq(offset, Immediate(state_ring_size()));
  __ setcc(below, scratch1);
  __ decq(scratch1);
  __ and_(scratch1, Immediate(state_ring_size()));
  __ subq(offset, scratch1);
}


void Codegen::FlowTime() {
  int offset = time_summary_size() - kPointerSize;

  if (offset - kPointerSize >= 0) {
    __ Move(scratch2, 0);
    __ Move(rcx, 1);
  }
    // TODO(rames): Use special opcode for 1 count. See intel spec
    // TODO(rames): Optimize.
  __ movq(scratch1, TimeSummary(offset));
  __ shr(scratch1, Immediate(1));
  __ movq(TimeSummary(offset), scratch1);
  for (offset = offset - kPointerSize; offset >= 0; offset--) {
    // TODO(rames): TEST.
    // TODO(rames): Optimize.
    __ setcc(carry, scratch2);
    __ movq(scratch1, TimeSummary(offset));
    __ shrd(scratch1, scratch2);
    __ movq(TimeSummary(offset), scratch1);
  }
}


void Codegen::CheckTimeFlow() {
  __ Move(scratch, 0);
  for (int offset = 0; offset < time_summary_size() ; offset += kPointerSize) {
    __ or_(scratch, TimeSummary(offset));
  }
}


void Codegen::Generate(RegexpInfo* rinfo,
                          MatchType match_type) {
  if (!CpuFeatures::initialized()) {
    CpuFeatures::Probe();
  }

  Label unwind_and_return;
  Label next_character, null_char;

  match_type_ = match_type;

  vector<Regexp*>::const_iterator it;
  vector<Regexp*>* gen_list = rinfo->gen_list();

  if (FLAG_print_re_list) {
    cout << "Regexp list --------------------------------{{{" << endl;
    for (it = gen_list->begin(); it < gen_list->end(); it++) {
      cout << **it << endl;
    }
    cout << "}}}------------------------- End of regexp list" << endl;
  }

  __ PushCalleeSavedRegisters();

  // Check that the base string we were passed is not null.
  __ cmpq(rdi, Immediate(0));
  __ debug_msg(zero, "base string is NULL.\n");
  __ j(zero, &unwind_and_return);
  if (!match_type != kMatchFull) {
    __ Move(rsi, 0);
  }

  // Check the match results pointer.
  if (match_type != kMatchFull && !FLAG_benchtest) {
    __ cmpq(rsi, Immediate(0));
    __ debug_msg(zero, "match results pointer is NULL.\n");
    __ j(zero, &unwind_and_return);
  }


  // The stack within this function is laid out as follow.
  //
  //        callee saved registers
  //  0x0   rbp caller's rbp.
  // -0x8   String base.
  // -0x10  Result match or vector of matches.
  // -0x18  Starting position for fast forwarding.
  // -0x20  State from which FF thinks there may be a potential match.
  // -0x28  Position of the end of the match when matching the regexp forward
  //        from the ff_element.
  // -0x30  Position of the end of the match when matching the regexp backward
  //        from the ff_element.
  // -0x38  Used when looking for multiple matches, indicates the end of the
  //        previous match.

  const size_t reserved_space =
    kStateInfoSize + state_ring_size() + time_summary_size();
  __ subq(rsp, Immediate(reserved_space));
  __ ZeroMem(rsp, rbp);

  __ movq(string_pointer, rdi);
  __ movq(ring_index, Immediate(0));

  __ movq(string_base, rdi);
  __ movq(result_matches, rsi);
  __ movq(ff_position, rdi);
  // Adjust for the initial character offset in FF.
  __ subq(ff_position, Immediate(kCharSize));


  if (match_type == kMatchFull) {
    SetStateForce(0, rinfo->entry_state());
    GenerateMatchForward(rinfo, match_type);

  } else if (FLAG_use_fast_forward) {
    Label fast_forward;

    __ bind(&fast_forward);
    if (GenerateFastForward(rinfo, match_type)) {
      // On output, either a state has been set from which there is a potential
      // match, or string_pointer points to the end of the string.

      // We were scanning forward up to this point.
      // The cache is probably hotter for previous characters than the rest of the
      // string, so match backward first.
      // Note that a few functions assume this order, so reversing backward and
      // forward matching will not work.
      GenerateMatchBackward(rinfo, match_type, &fast_forward);

      GenerateMatchForward(rinfo, match_type, &fast_forward);
    } else {
      GenerateMatchForward(rinfo, match_type, NULL);
    }

  } else {
    // TODO(rames): need to Re-architecture after big fast forward refactoring.
    UNIMPLEMENTED();
    GenerateMatchForward(rinfo, match_type, NULL);
  }

  // Unwind the stack and return.
  __ cld();
  __ bind(&unwind_and_return);
  __ PopCalleeSavedRegisters();
  __ ret(0);
}


bool Codegen::GenerateFastForward(RegexpInfo* rinfo, MatchType match_type) {
  vector<Regexp*>* fflist = rinfo->ff_list();
  FF_finder fff(rinfo->regexp(), fflist);
  bool ff_success = fff.Visit(rinfo->regexp());

  if (FLAG_print_ff_elements) {
    cout << "Fast forward elements ----------------------{{{" << endl;
    { IndentScope indent;
      if (ff_success) {
        vector<Regexp*>::iterator ffit;
        for (ffit = fflist->begin(); ffit < fflist->end(); ffit++) {
          cout << *(*ffit) << endl;
        }
      } else {
        Indent(cout) << "ff failed" << endl;
      }
    }
    cout << "}}}--------------- End of fast forward elements" << endl;
  }

  if (ff_success) {
    FastForwardGen ffgen(this, rinfo->ff_list());
    // TODO: Do we need to increment here? It seems we are compensating
    // everywhere by decrementing.
    __ movq(string_pointer, ff_position);
    __ inc_c(string_pointer);
    // Clear the temporary matches.
    __ movq(backward_match, Immediate(0));
    __ movq(forward_match,  Immediate(0));
    __ movq(last_match_end, Immediate(0));
    ffgen.Generate();
    __ movq(ff_position, string_pointer);
  }
  return ff_success;
}


void Codegen::HandleControlRegexps(RegexpInfo* rinfo) {
  vector<Regexp*>::iterator it;
  for (it = rinfo->gen_list()->begin(); it < rinfo->gen_list()->end(); it++) {
    if ((*it)->IsControlRegexp()) {
      Visit(*it);
    }
  }
}


void Codegen::CheckMatch(Direction direction,
                            RegexpInfo* rinfo,
                            MatchType match_type,
                            Label* limit,
                            Label* exit) {
  switch (match_type) {
    case kMatchFull:
      // Nothing to do here.
      // For full matches exits are:
      //  - when hitting the terminating '\0' or the beginning of the string.
      //  - when time stops flowing.
      break;

    case kMatchAnywhere: {
      // Early exit as soon as we find a match.
      Label no_match;

      if (direction == kBackward) {
        TestState(0, rinfo->entry_state());
        __ j(zero, &no_match);

        ClearAllTimes();
        __ movq(scratch, ff_found_state);
        SetStateForce(0, scratch);
        __ movq(string_pointer, ff_position);
        __ jmp(exit);

        __ bind(&no_match);

      } else {
        __ movq(rax, Immediate(1));
        TestState(0, rinfo->output_state());
        __ j(not_zero, exit);
      }

      break;
    }

    case kMatchFirst: {
      Label no_match;
      // A match is not an exit situation, as longer matches may occur.
      Operand remember_match = direction == kBackward ? backward_match
        : forward_match;
      int match_state =
        direction == kBackward ? rinfo->entry_state() : rinfo->output_state();
      TestState(0, match_state);
      __ j(zero, &no_match);

      __ movq(remember_match, string_pointer);

      __ bind(&no_match);
      break;
    }

    case kMatchAll: {
      Label no_match;
      // A match is not an exit situation, as longer matches may occur.
      Operand remember_match = direction == kBackward ? backward_match
        : forward_match;
      int match_state =  direction == kBackward ? rinfo->entry_state()
        : rinfo->output_state();

      if (direction == kBackward) {
        // A new match cannot start before the latest match finishes.
        __ cmpq(string_pointer, last_match_end);
        __ j(below, limit);
      }

      TestState(0, match_state);
      __ j(zero, &no_match);

      __ movq(remember_match, string_pointer);

      __ bind(&no_match);
      break;
    }

    default:
      UNREACHABLE();
  }
}


void Codegen::GenerateMatchDirection(Direction direction,
                                        RegexpInfo* rinfo,
                                        MatchType match_type,
                                        Label* fast_forward) {
  Label next_character, advance, limit, exit;
  vector<Regexp*>* gen_list = rinfo->gen_list();
  vector<Regexp*>::const_iterator it;

  set_direction(direction);

  __ bind(&next_character);

  if (match_type != kMatchFull && !fast_forward) {
    SetStateForce(0, rinfo->entry_state());
  }

  HandleControlRegexps(rinfo);

  CheckMatch(direction, rinfo, match_type, &limit, &exit);

  if (direction == kForward) {
    __ cmpb(current_char, Immediate(0));
    __ j(equal, &limit);
  } else {
    __ cmpq(string_pointer, string_base);
    __ j(equal, &limit);
  }

  CheckTimeFlow();
  switch (match_type_) {
    case kMatchFull: {
      Label cont;
      __ j(not_zero, &cont);
      __ Move(rax, 0);
      __ jmp(&exit);
      __ bind(&cont);
      break;
    }

    case kMatchAnywhere:
    case kMatchFirst:
    case kMatchAll: {
      if (fast_forward) {
        // TODO(rames): move out the current char == 0 test.
        // It can only occur the first time in the first loop.
        Label keep_searching;
        __ j(not_zero, &keep_searching);

        // Check for the current char before the next one to avoid illegally
        // accessing memory after the end of the string.
        __ cmpb(current_char, Immediate(0));
        __ j(zero, &limit);
        // TODO: Comment on this. Why do we need it?
        __ cmpb(next_char, Immediate(0));
        __ j(zero, &limit);

        if (match_type == kMatchAnywhere) {
          __ jmp(fast_forward);

        } else if (match_type == kMatchFirst) {
          Operand remembered_match =
            direction == kBackward ? backward_match : forward_match;
          __ cmpq(remembered_match, Immediate(0));
          __ j(zero, fast_forward);
          __ jmp(&limit);

        } else {  // kMatchAll
          Operand remembered_match = direction == kBackward ? backward_match
            : forward_match;
          __ cmpq(remembered_match, Immediate(0));
          __ j(zero, fast_forward);


          if (direction == kBackward) {
            __ movq(string_pointer, backward_match);
            __ movq(scratch2, ff_found_state);
            SetStateForce(0, scratch2);
            __ movq(string_pointer, ff_position);
            __ jmp(&exit);

          } else {
            // TOTO(rames): Merge code with CheckMatch?
            Register match = rdi;
            __ movq(match, result_matches);
            __ cmpq(match, Immediate(0));
            __ j(zero, &keep_searching);
            __ movq(rdx, forward_match);
            __ movq(last_match_end, rdx);
            __ movq(rsi, backward_match);
            __ movq(ff_position, rdx);
            // We normally decrement the ff_position to account for the
            // increment when entering ff.
            // When the match has a length of zero, we need to artificially
            // increment the ff_position to avoid matching at the same position
            // again.
            __ Move(scratch, 0);
            __ cmpq(rdx, rsi);
            __ setcc(not_equal, scratch);
            // TODO: Correct for kCharSize.
            __ subq(ff_position, scratch);
            __ CallCpp(FUNCTION_ADDR(RegisterMatch));
            ClearAllTimes();
          }


          __ jmp(fast_forward);
        }
        __ bind(&keep_searching);

      } else {
        // TODO(rames): Optimize.
        // For now do nothing and run the slow case.
      }

      break;
    }

    default:
      UNREACHABLE();
  }

  for (it = gen_list->begin(); it < gen_list->end(); it++) {
    // Control regexps are handled in HandleControlRegexps().
    if (!(*it)->IsControlRegexp()) {
      Visit(*it);
    }
  }

  ClearTime(0);


  __ bind(&advance);
  // Advance the string index.
  __ Move(scratch1, 0);
  Advance(1);
  __ addq(ring_index, Immediate(state_ring_time_size()));
  __ cmpq(ring_index, Immediate(state_ring_size()));
  __ setcc(below, scratch1);
  __ decq(scratch1);
  __ and_(scratch1, Immediate(state_ring_size()));
  __ subq(ring_index, scratch1);

  FlowTime();

  __ jmp(&next_character);


  __ bind(&limit);
  if (match_type == kMatchFull) {
    __ Move(rax, 0);
    TestState(0, rinfo->output_state());
    __ setcc(not_equal, rax);

  } else if (match_type == kMatchAnywhere) {
      ClearAllTimes();
      __ Move(rax, 0);
      __ movq(ff_found_state, Immediate(-1));

  } else {
    if (direction == kForward) {
      __ Move(rax, 0);
      __ cmpq(forward_match, Immediate(0));
      __ j(zero, &exit);

      // We have a match!
      __ Move(rax, 1);

      if (match_type == kMatchFirst) {
        Register match = scratch3;
        __ movq(match, result_matches);
        __ cmpq(match, Immediate(0));
        __ j(zero, &exit);

        __ movq(scratch1, backward_match);
        __ movq(scratch2, forward_match);
        __ movq(Operand(match, offsetof(Match, begin)), scratch1);
        __ movq(Operand(match, offsetof(Match, end)),   scratch2);

      } else {  // kMatchAll
        Label keep_searching;
        Register match = rdi;

        // Register the match.
        // rsi: exit start.
        __ movq(match, result_matches);
        __ cmpq(match, Immediate(0));
        __ j(zero, &keep_searching);
        // TODO(rames): should the string pointer be rdx?
        __ movq(rdx, forward_match);
        __ movq(last_match_end, rdx);
        __ movq(rsi, backward_match);
        __ movq(ff_position, rdx);
        __ Move(scratch, 0);
        // Correct ff_position for matches of null lengths to avoid matching
        // them again. This is fine because if there was any match of length
        // greater than zero from there, it should have matched instead.
        __ cmpq(rdx, rsi);
        __ setcc(not_equal, scratch);
        // TODO: Correct for kCharSize.
        __ subq(ff_position, scratch);
        __ CallCpp(FUNCTION_ADDR(RegisterMatch));
        ClearAllTimes();
        // If the ff_position is already at the eos, we should exit here.
        // Continuing to ff would process past eos.
        __ movq(scratch, ff_position);
        __ cmpb(Operand(scratch, 0), Immediate(0));
        __ j(zero, &exit);
        __ jmp(fast_forward);

        __ bind(&keep_searching);
      }


    } else {
      Label match;
      ClearAllTimes();
      // Check if we found a match but were hoping to find a longer one.
      __ cmpq(backward_match, Immediate(0));
      __ j(not_zero, &match);
      __ movq(ff_found_state, Immediate(-1));
      __ jmp(&exit);

      __ bind(&match);
      __ movb(rax, Immediate(1));
      __ movq(string_pointer, backward_match);
      __ movq(scratch2, ff_found_state);
      SetStateForce(0, scratch2);
      __ movq(string_pointer, ff_position);
    }
  }

  __ bind(&exit);
}


void Codegen::GenerateMatchBackward(RegexpInfo* rinfo,
                                    MatchType match_type,
                                    Label* fast_forward) {
  if (fast_forward &&
      all_regexps_start_at(rinfo->entry_state(), rinfo->ff_list())) {
    __ movq(backward_match, string_pointer);
  } else {
    GenerateMatchDirection(kBackward, rinfo, match_type, fast_forward);
  }
}


void Codegen::VisitEpsilon(Epsilon* epsilon) {
  DirectionSetOutputFromEntry(0, epsilon);
}


// Falls through when no matches is found.
static void MatchStartOrEndOfLine(MacroAssembler* masm_,
                                  ControlRegexp* seol,
                                  const Operand& char_pos,
                                  Label* on_match,
                                  bool char_loaded_in_rax = false,
                                  Label* on_start_of_string = NULL) {
  if (on_start_of_string) {
    ASSERT(seol->IsStartOfLine());
    __ cmpq(string_pointer, string_base);
    __ j(equal, on_start_of_string);
  }
  if (!char_loaded_in_rax) {
    __ movb(rax, char_pos);
  }
  __ cmpb_al(Immediate('\n'));
  __ j(equal, on_match);
  __ cmpb_al(Immediate('\r'));
  __ j(equal, on_match);
  if (seol->IsEndOfLine()) {
    __ cmpb(current_char, Immediate('\0'));
    __ j(equal, on_match);
  }
}


void Codegen::VisitStartOfLine(StartOfLine* sol) {
  Label match, done;

  MatchStartOrEndOfLine(masm_, sol, previous_char, &match, false, &match);
  __ jmp(&done);

  __ bind(&match);
  DirectionSetOutputFromEntry(0, sol);
  __ bind(&done);
}


void Codegen::VisitEndOfLine(EndOfLine* eol) {
  Label match, done;

  MatchStartOrEndOfLine(masm_, eol, current_char, &match);
  __ jmp(&done);

  __ bind(&match);
  DirectionSetOutputFromEntry(0, eol);
  __ bind(&done);
}


// Try to match mc from the current string_pointer position.
// string_pointer is not modified.
// On output the condition flags will match equal/not_equal depending on wether
// there is a match or not.
static void MatchMultipleChar(MacroAssembler *masm_,
                              Direction direction,
                              MultipleChar* mc) {
  // The implementation must not expect anything regarding the alignment of the
  // strings.
  // TODO: Implement a SIMD path.
  unsigned n_chars = mc->chars_length();
  if (n_chars <= 8 && direction == kForward) {
    __ cmp(mc->chars_length(), current_chars, mc->imm_chars());
  } else {
    __ movq(rsi, string_pointer);
    // TODO(rames): This assumes that the regexp is still accessible at
    // runtime (mc->chars() points to the regexp string).
    if (direction == kForward) {
      __ Move(rdi, (uint64_t)(mc->chars()));
    } else {
      __ Move(rdi, (uint64_t)(mc->chars() + n_chars - 1));
    }
    // TODO(rames): Work out the best options depending on the maximum number of
    // characters to match.
    if (n_chars >= 8) {
      __ Move(rcx, n_chars / 8);
      __ repnecmpsq();
    }
    if (n_chars % 8 > 0) {
      __ Move(rcx, n_chars % 8);
      __ repnecmpsb();
    }
  }
}


void Codegen::VisitMultipleChar(MultipleChar* mc) {
  Label no_match;
  unsigned n_chars = mc->chars_length();

  // If matching backward, there is no terminating character to help the match
  // fail.
  if (direction() == kBackward) {
    __ dec_c(string_pointer);
    __ movq(scratch, string_pointer);
    __ subq(scratch, Immediate(n_chars));
    __ cmpq(scratch, string_base);
    __ j(below, &no_match);
  }

  MatchMultipleChar(masm_, direction(), mc);
  __ j(not_equal, &no_match);

  DirectionSetOutputFromEntry(n_chars, mc);
  __ bind(&no_match);
  if (direction() == kBackward) {
    __ inc_c(string_pointer);
  }
}


void Codegen::VisitPeriod(Period* period) {
  // Match all characters exept '\n' and '\r'.
  Label no_match;

  if (direction() == kBackward) {
    __ dec_c(string_pointer);
    // TODO(rames): need this for others.
    __ cmpq(string_pointer, string_base);
    __ j(below, &no_match);
  }

  __ cmpb(current_char, Immediate('\n'));
  __ j(equal, &no_match);
  __ cmpb(current_char, Immediate('\r'));
  __ j(equal, &no_match);

  DirectionSetOutputFromEntry(1, period);

  __ bind(&no_match);
  if (direction() == kBackward) {
    __ inc_c(string_pointer);
  }
}


// If the current character matches, jump to 'on_matching_char', else fall
// through.
static void MatchBracket(MacroAssembler *masm_,
                         const Operand& c,
                         Bracket* bracket,
                         Label* on_matching_char,
                         Label* on_eos = NULL) {
  __ movb(rax, c);

  if (on_eos) {
    __ cmpb_al(Immediate(0));
    __ j(equal, on_eos);
  }

  vector<char>::const_iterator it;
  for (it = bracket->single_chars()->begin();
       it < bracket->single_chars()->end();
       it++) {
    __ cmpb_al(Immediate(*it));
    __ j(equal, on_matching_char);
  }

  vector<Bracket::CharRange>::const_iterator rit;
  for (rit = bracket->char_ranges()->begin();
       rit < bracket->char_ranges()->end();
       rit++) {
    __ cmpb_al(Immediate((*rit).low));
    __ setcc(greater_equal, rbx);
    __ cmpb_al(Immediate((*rit).high));
    __ setcc(less_equal, rcx);
    __ andb(rbx, rcx);
    __ j(not_zero, on_matching_char);
  }
}


void Codegen::VisitBracket(Bracket* bracket) {
  Label match, no_match;

  bool non_matching = bracket->flags() & Bracket::non_matching;
  Label* on_matching_char = non_matching ? &no_match : &match;

  if (direction() == kBackward) {
    __ cmpq(string_pointer, string_base);
    __ j(equal, &no_match);
  }

  MatchBracket(masm_,
               direction() == kBackward ? previous_char : current_char,
               bracket, on_matching_char);
  if (!non_matching) {
    __ jmp(&no_match);
  }

  __ bind(&match);
  DirectionSetOutputFromEntry(1, bracket);
  __ bind(&no_match);
}


// TODO(rames): optimize when state_ring_size is a power of 2.
void Codegen::TestState(int time, int state_index) {
  ASSERT(time >= 0);
  if (time != 0) {
    ComputeStateOperandOffset(scratch2, time, state_index);
    __ cmpq(StateOperand(scratch2), Immediate(0));
  } else {
    __ cmpq(StateOperand(0, state_index), Immediate(0));
  }
}


// Set a state. Only update if the current match source is 'older' than the
// target match source.
// TODO(rames): Use CMOVcc instad of conditional jump?
void Codegen::SetState(int target_time,
                          int target_index,
                          int source_index) {
  ASSERT(target_time >= 0);

  if (target_time == 0) {
    Label skip;
    __ movq(scratch1, StateOperand(0, source_index));
    __ decq(scratch1);
    __ movq(scratch2, StateOperand(0, target_index));
    __ decq(scratch2);
    __ cmpq(scratch1, scratch2);
    __ j(above_equal, &skip);
    __ movq(scratch1, StateOperand(0, source_index));
    __ movq(StateOperand(0, target_index), scratch1);
    __ or_(TimeSummaryOperand(target_time),
           Immediate(1 << (target_time % kBitsPerByte)));
    __ bind(&skip);

  } else {
    Label skip;
    Register target_offset = scratch4;
    ComputeStateOperandOffset(target_offset, target_time, target_index);

    __ movq(scratch1, StateOperand(0, source_index));
    __ decq(scratch1);
    __ movq(scratch2, StateOperand(target_offset));
    __ decq(scratch2);
    __ cmpq(scratch1, scratch2);
    __ j(above_equal, &skip);
    __ movq(scratch1, StateOperand(0, source_index));
    __ movq(StateOperand(target_offset), scratch1);
    __ or_(TimeSummaryOperand(target_time),
           Immediate(1 << (target_time % kBitsPerByte)));
    __ bind(&skip);
  }
}


void Codegen::SetStateForce(int target_time, int target_index) {
  ASSERT(target_time >= 0);

  if (target_time == 0) {
    __ movq(StateOperand(0, target_index), string_pointer);

  } else {
    // We are not using this yet.
    UNIMPLEMENTED();
  }
  __ or_(TimeSummaryOperand(target_time),
         Immediate(1 << (target_time % kBitsPerByte)));
}


void Codegen::SetStateForce(int target_time, Register target_index) {
  ASSERT(target_time >= 0);

  if (target_time == 0) {
    __ movq(StateOperand(0, target_index), string_pointer);

  } else {
    // We are not using this yet.
    UNIMPLEMENTED();
  }
  __ or_(TimeSummaryOperand(target_time),
         Immediate(1 << (target_time % kBitsPerByte)));
}


void Codegen::DirectionTestEntryState(int time, Regexp* regexp) {
  TestState(0, direction_ == kForward ? regexp->entry_state()
                                      : regexp->output_state());
}


void Codegen::DirectionSetOutputFromEntry(int time, Regexp* regexp) {
  if (direction() == kForward) {
    SetState(time, regexp->output_state(), regexp->entry_state());
  } else {
    SetState(time, regexp->entry_state(), regexp->output_state());
  }
}


void Codegen::ClearTime(int time) {
  // TODO(rames): Use a loop.
  if (time == 0) {
    for (int offset = 0;
         offset < state_ring_time_size();
         offset += kPointerSize) {
      __ movq(Operand(StateOperand(ring_index), offset), Immediate(0));
    }

  } else {
    __ movq(scratch, ring_index);
    __ addq(scratch, Immediate(time * state_ring_time_size()));

    Label no_wrapping;
    __ cmpq(scratch, Immediate(state_ring_size()));
    __ j(less, &no_wrapping);
    __ subq(scratch, Immediate(state_ring_size()));
    __ bind(&no_wrapping);

    for (int offset = 0;
         offset < state_ring_time_size();
         offset += kPointerSize) {
    __ movq(Operand(StateOperand(scratch), offset), Immediate(0));
    }
  }

  // Clear the summary bit.
  // TODO(rames): Use BTR.
  // TODO(rames): not necessary as shifted away for time 0?
  __ and_(TimeSummaryOperand(time), Immediate(~(1 << (time % kBitsPerPointer))));
}


void Codegen::ClearAllTimes() {
  // TODO(rames): Use a loop.
  int offset;
  for (offset = 0; offset < state_ring_size(); offset+= kPointerSize) {
    __ movq(Operand(rbp, StateRingBaseOffsetFromFrame() + offset),
            Immediate(0));
  }
  for (offset = 0; offset < time_summary_size() ; offset += kPointerSize) {
    __ movq(TimeSummary(offset), Immediate(0));
  }
}


void Codegen::set_direction(Direction dir) {
  direction_ = dir;
  if (direction() == kForward) {
    __ cld();
  } else {
    __ std();
  }
}


// FastForwardGen --------------------------------------------------------------

void FastForwardGen::Generate() {
  if (regexp_list_->size() == 0) {
    return;
  }

  if (regexp_list_->size() == 1) {
    VisitSingle(regexp_list_->at(0));

  } else {

    vector<Regexp*>::iterator it;
    bool multiple_chars_only = true;
    for (it = regexp_list_->begin(); it < regexp_list_->end(); it++) {
      if (!(*it)->IsMultipleChar()) {
        multiple_chars_only = false;
        break;
      }
    }

    // We currently only support a SIMD path for alternations of MultipleChars.
    // TODO: Add support for alternations of other regexps. This should simpler
    // with the new code structure.
    if (CpuFeatures::IsAvailable(SSE4_2) &&
        multiple_chars_only &&
        regexp_list_->size() < XMMRegister::kNumRegisters - 1) {
      // Pre-load the XMM registers for MultipleChars.
      static const int first_free_xmm_code = 4;
      for (unsigned i = 0; i < regexp_list_->size(); i++) {
        __ movdqp(XMMRegister::from_code(first_free_xmm_code + i),
                  regexp_list_->at(i)->AsMultipleChar()->chars(),
                  regexp_list_->at(i)->AsMultipleChar()->chars_length());
      }

      Label align_string_pointer, align_loop;
      Label simd_code, simd_loop;
      Label maybe_match, keep_searching, find_null, exit;

      __ bind(&align_string_pointer);
      // Align the string pointer on a 16 bytes boundary before entering the
      // SIMD path.
      // Along with the use of movdqa below, it allows to avoid potentially
      // illegal accesses after the eos.
      // TODO: This alignment code should be abstracted and shared with other
      // places.
      __ dec_c(string_pointer);
      __ bind(&align_loop);
      __ inc_c(string_pointer);
      __ cmpb(current_char, Immediate(0));
      __ j(zero, &exit);
      // Check the alignment.
      __ testq(string_pointer, Immediate(0xf));
      __ j(zero, &simd_code);
      // Check for matches.
      for (unsigned i = 0; i < regexp_list_->size(); i++) {
        Label no_match;
        MultipleChar *mc = regexp_list_->at(i)->AsMultipleChar();
        MatchMultipleChar(masm_, kForward, mc);
        __ j(not_equal, &no_match);
        FoundState(0, mc->entry_state());
        __ jmp(&exit);
        __ bind(&no_match);
      }
      __ jmp(&align_loop);


      __ bind(&simd_code);
      // At this point we are certain that the string pointer is 16 bytes
      // aligned.
      if (FLAG_emit_debug_code) {
        __ testq(string_pointer, Immediate(0xf));
        __ asm_assert(zero, "string_pointer must be 16 bytes aligned.");
      }

      static const uint8_t pcmp_str_control =
        Assembler::unsigned_bytes | Assembler::equal_order |
        Assembler::pol_pos | Assembler::lsi;

      Label match_somewhere_0x00, match_somewhere_0x10,
            match_somewhere_0x20, match_somewhere_0x30;
      XMMRegister xmm_s_0x00 = xmm0;
      XMMRegister xmm_s_0x10 = xmm1;
      XMMRegister xmm_s_0x20 = xmm2;
      XMMRegister xmm_s_0x30 = xmm3;

      // Load the first 16 bytes.
      // After that xmm_s_0x00 will be preloaded from the last round of the fast
      // loop.
      __ movdqa(xmm_s_0x00, Operand(string_pointer, 0));

      // This loop scans the code for eos or potential match.
      // It makes no distinction between potential matches to be able to scan as
      // fast as possible. When a potential match is detected, we hand control
      // to a more thorough code.
      __ bind(&simd_loop);

#define fast_round(current_offset, next_offset, xmm_next)                      \
      for (unsigned i = 0; i < regexp_list_->size(); i++) {                    \
        __ pcmpistri(pcmp_str_control,                                         \
                     XMMRegister::from_code(first_free_xmm_code + i),          \
                     xmm_s_##current_offset);                                  \
        __ j(below_equal, &match_somewhere_##current_offset);                  \
        if (i == 0) {                                                          \
          /* The conditional jump above ensures that the eos wasn't reached. */\
          __ movdqa(xmm_s_##xmm_next, Operand(string_pointer, next_offset));   \
        }                                                                      \
      }
      fast_round(0x00, 0x10, 0x10)
      fast_round(0x10, 0x20, 0x20)
      fast_round(0x20, 0x30, 0x30)
      fast_round(0x30, 0x40, 0x00)

      __ addq(string_pointer, Immediate(0x40));
      __ jmp(&simd_loop);

      __ bind(&match_somewhere_0x30);
      __ addq(string_pointer, Immediate(0x10));
      __ bind(&match_somewhere_0x20);
      __ addq(string_pointer, Immediate(0x10));
      __ bind(&match_somewhere_0x10);
      __ addq(string_pointer, Immediate(0x10));
      __ bind(&match_somewhere_0x00);


      // We know there is a potential match or eos somewhere between in
      // [string_pointer : string_pointer + 0x10].
      // Find at what index this potential match is.
      Register low_index = scratch2;
      Register null_found = scratch3;
      __ Move(low_index, 0x10);
      __ Move(null_found, 0);

      __ movdqa(xmm0, Operand(string_pointer, 0));
      for (unsigned i = 0; i < regexp_list_->size(); i++) {
        __ pcmpistri(pcmp_str_control,
                     XMMRegister::from_code(first_free_xmm_code + i), xmm0);
        __ setcc(zero, scratch);
        __ or_(null_found, scratch);
        __ cmpq(rcx, low_index);
        __ cmovq(below, low_index, rcx);
      }

      // pcmpistr[im] takes care of invalidating matches after the eos if it was
      // found.
      __ cmpq(low_index, Immediate(0x10));
      __ j(not_equal, &maybe_match);

      __ cmpb(null_found, Immediate(0));
      __ j(zero, &keep_searching);
      // There is no potential match, and we found the null character.
      // Advance the string pointer up to the null character and exit.
      __ AdvanceToEOS();
      __ jmp(&exit);

      __ bind(&keep_searching);
      __ addq(string_pointer, Immediate(0x10));
      __ jmp(&simd_loop);

      __ bind(&maybe_match);
      // We have a potential match.
      // Check if it is good enough to exit the fast forward loop.
      __ addq(string_pointer, low_index);
      for (unsigned i = 0; i < regexp_list_->size(); i++) {
        // TODO: We could use pre-loaded registers if there are enough. Or maybe
        // move from xmm registers if it is faster.
        Label no_match;
        MultipleChar *mc = regexp_list_->at(i)->AsMultipleChar();
        __ MoveCharsFrom(scratch, mc->chars_length(), mc->chars());
        __ cmp_truncated(mc->chars_length(), scratch, Operand(string_pointer, 0));
        __ j(not_equal, &no_match);
        FoundState(0, mc->entry_state());
        __ jmp(&exit);
        __ bind(&no_match);
      }
      __ jmp(&align_string_pointer);

      __ bind(&exit);

    } else {
      Label loop, potential_match;
      potential_match_ = &potential_match;

      __ bind(&loop);

      for (it = regexp_list_->begin(); it < regexp_list_->end(); it++) {
        Visit(*it);
      }

      // We must check for eos after having visited the ff elements, because eos
      // may be a potential match for one of them.
      __ cmpb(current_char, Immediate(0));
      __ j(zero, &potential_match);

      __ inc_c(string_pointer);
      __ jmp(&loop);

      __ bind(&potential_match);
    }
  }
}


void FastForwardGen::FoundState(int time, int state) {
  __ movq(ff_found_state, Immediate(state));
  if (state >= 0) {
    codegen_->SetStateForce(time, state);
  }
}


// Single visitors ---------------------------------------------------

void FastForwardGen::VisitSingleMultipleChar(MultipleChar* mc) {
  int n_chars = mc->chars_length();

  if (CpuFeatures::IsAvailable(SSE4_2)) {
    Label align, align_loop;
    Label simd_code, simd_loop;
    Label potential_match, check_potential_match, found, exit;


    Register fixed_chars = scratch3;
    __ MoveCharsFrom(fixed_chars, n_chars, mc->chars());

    // Align the string pointer on a 16 bytes boundary.
    // TODO: use SIMD here.
    __ bind(&align);
    __ dec_c(string_pointer);
    __ bind(&align_loop);
    __ inc_c(string_pointer);
    __ cmpb(current_char, Immediate(0));
    __ j(zero, &exit);
    __ testq(string_pointer, Immediate(0xf));
    __ j(zero, &simd_code);
    // For speed, use a fast pre-check and defer further verification to more
    // thorough code.
    __ cmp_truncated(n_chars, fixed_chars, current_chars);
    __ j(equal, &check_potential_match);

    __ jmp(&align_loop);


    __ bind(&simd_code);
    Label offset_0x0, offset_0x10, offset_0x20;
    static const uint8_t pcmp_str_control =
      Assembler::unsigned_bytes | Assembler::equal_order |
      Assembler::pol_pos | Assembler::lsi;

    __ movdqp(xmm0, mc->chars(), n_chars);

    __ movdqa(xmm1, Operand(string_pointer, 0));

    __ bind(&simd_loop);
    __ pcmpistri(pcmp_str_control, xmm0, xmm1);
    __ j(below_equal, &offset_0x0);
    __ movdqa(xmm2, Operand(string_pointer, 0x10));
    __ pcmpistri(pcmp_str_control, xmm0, xmm2);
    __ j(below_equal, &offset_0x10);
    __ movdqa(xmm3, Operand(string_pointer, 0x20));
    __ pcmpistri(pcmp_str_control, xmm0, xmm3);
    __ j(below_equal, &offset_0x20);
    __ movdqa(xmm1, Operand(string_pointer, 0x30));
    __ addq(string_pointer, Immediate(0x30));
    __ jmp(&simd_loop);

    __ bind(&offset_0x20);
    __ addq(string_pointer, Immediate(0x10));
    __ bind(&offset_0x10);
    __ addq(string_pointer, Immediate(0x10));
    __ bind(&offset_0x0);
    __ cmpq(rcx, Immediate(0x10));
    __ j(not_equal, &potential_match);

    __ AdvanceToEOS();
    __ jmp(&exit);

    __ bind(&potential_match);
    __ addq(string_pointer, rcx);

    __ cmp_truncated(n_chars, fixed_chars, Operand(string_pointer, 0));
    __ j(not_equal, &align);

    __ bind(&check_potential_match);
    MatchMultipleChar(masm_, kForward, mc);
    __ j(not_equal, &align_loop);

    __ bind(&found);
    FoundState(0, mc->entry_state());
    __ bind(&exit);

  } else {
    Label loop, done, exit;
    Register fixed_chars = scratch3;

    __ MoveCharsFrom(fixed_chars, n_chars, mc->chars());
    __ dec_c(string_pointer);

    // TODO(rames): Is it more efficient to increment an offset rather than the
    // register?
    __ bind(&loop);
    __ inc_c(string_pointer);
    __ mov_truncated(n_chars, rax, current_char);
    __ cmpb_al(Immediate(0));
    __ j(equal, &exit);
    __ cmp_truncated(n_chars, rax, fixed_chars);
    __ j(not_equal, &loop);

    __ bind(&done);
    FoundState(0, mc->entry_state());
    __ bind(&exit);
  }
}


// TODO(rames): slow single visitors are all too similar not to be refactord!
void FastForwardGen::VisitSinglePeriod(Period* period) {
  // TODO(rames): we probably never want to have a single ff for a period!!
  // Need to refactor the ff finder mechanisms.
  Label loop, done;

  __ dec_c(string_pointer);

  __ bind(&loop);
  __ inc_c(string_pointer);

  __ movb(rax, current_char);
  __ cmpb_al(Immediate(0));
  __ j(equal, &done);

  __ cmpb_al(Immediate('\n'));
  __ j(equal, &loop);
  __ cmpb_al(Immediate('\r'));
  __ j(equal, &loop);

  __ bind(&done);
  FoundState(0, period->entry_state());
}


void FastForwardGen::VisitSingleBracket(Bracket* bracket) {
  // TODO(rames): Allow more complex brackets.
//  if (CpuFeatures::IsAvailable(SSE4_2) &&
//      bracket->single_chars()->size() < 16 &&
//      bracket->char_ranges()->size() < 8) {
//
//
//    uint64_t single_chars_ctrl =
//      Assembler::unsigned_bytes | Assembler::equal_any |
//      Assembler::pol_pos | Assembler::lsi;
//    uint64_t ranges_ctrl =
//      Assembler::unsigned_bytes | Assembler::ranges |
//      Assembler::pol_pos | Assembler::lsi;
//
//
//    uint64_t high = 0;
//    uint64_t low = 0;
//
//    // Don't assume how vector storage works...?
//    // TODO(rames): abstract
//    if (bracket->single_chars()->size()) {
//      for (unsigned i = 0; i < bracket->single_chars()->size() && i < 8; i++) {
//        low |= bracket->single_chars()->at(i) << (i * 8);
//      }
//      for (unsigned i = 8; i < bracket->single_chars()->size() && i < 16; i++) {
//        high |= bracket->single_chars()->at(i) << ((i - 8) * 8);
//      }
//      __ movdq(xmm0, high, low);
//    }
//
//    if (bracket->char_ranges()->size()) {
//      for (unsigned i = 0; i < bracket->char_ranges()->size() && i < 4; i++) {
//        low |= bracket->char_ranges()->at(i).low << (i * 8);
//        low |= bracket->char_ranges()->at(i).high << (i * 8 + 8);
//      }
//      for (unsigned i = 4; i < bracket->char_ranges()->size() && i < 8; i++) {
//        high |= bracket->char_ranges()->at(i).low << ((i - 4) * 8);
//        high |= bracket->char_ranges()->at(i).high << ((i - 4) * 8 + 8);
//      }
//      __ movdq(xmm1, high, low);
//    }
//
//
//    Label simd_loop, p1, p2, p3, match, exit;
//
//    __ movq(rax, string_pointer);
//
//    if (bracket->single_chars()->size() && bracket->char_ranges()->size()) {
//      UNIMPLEMENTED();
//
//    } else if (bracket->single_chars()->size()) {
//    __ movdqu(xmm1, Operand(rax, 0));
//
//    __ bind(&simd_loop);
//
//    __ pcmpistri(single_chars_ctrl, xmm0, xmm1);
//    __ movdqu(xmm2, Operand(rax, 0x10));
//    __ j(below_equal, &p1);
//
//    __ pcmpistri(single_chars_ctrl, xmm0, xmm2);
//    __ movdqu(xmm3, Operand(rax, 0x20));
//    __ j(below_equal, &p2);
//
//    __ pcmpistri(single_chars_ctrl, xmm0, xmm3);
//    __ movdqu(xmm1, Operand(rax, 0x30));
//    __ j(below_equal, &p3);
//    __ addq(rax, Immediate(0x30));
//
//    __ jmp(&simd_loop);
//
//    __ bind(&p3);
//    __ addq(rax, Immediate(0x10));
//    __ bind(&p2);
//    __ addq(rax, Immediate(0x10));
//    __ bind(&p1);
//
//    } else if (bracket->char_ranges()->size()) {
//    __ movdqu(xmm2, Operand(rax, 0));
//
//    __ bind(&simd_loop);
//
//    __ pcmpistri(ranges_ctrl, xmm1, xmm2);
//    __ movdqu(xmm3, Operand(rax, 0x10));
//    __ j(below_equal, &p1);
//
//    __ pcmpistri(ranges_ctrl, xmm1, xmm3);
//    __ movdqu(xmm4, Operand(rax, 0x20));
//    __ j(below_equal, &p2);
//
//    __ pcmpistri(ranges_ctrl, xmm1, xmm4);
//    __ movdqu(xmm2, Operand(rax, 0x30));
//    __ j(below_equal, &p3);
//    __ addq(rax, Immediate(0x30));
//
//    __ jmp(&simd_loop);
//
//    __ bind(&p3);
//    __ addq(rax, Immediate(0x10));
//    __ bind(&p2);
//    __ addq(rax, Immediate(0x10));
//    __ bind(&p1);
//
//    }
//
//
//    __ movq(string_pointer, rax);
//
//    __ cmpq(rcx, Immediate(16));
//    __ j(not_equal, &match);
//    Label find_null;
//    __ dec_c(string_pointer);
//    __ bind(&find_null);
//    __ inc_c(string_pointer);
//    __ movb(rax, Operand(string_pointer, 0));
//    __ cmpb_al(Immediate(0));
//    __ j(not_zero, &find_null);
//    __ jmp(&exit);
//
//    __ bind(&match);
//    FoundState(0, bracket->entry_state());
//    __ bind(&exit);
//
//
//
//    //__ pcmpistri(single_chars_ctrl, xmm0, xmm1);
//    //__ addq(rax, Immediate(0x10));
//    //__ movq(rbx, rcx);
//    //__ setcc(below_equal, scratch1);
//
//    //__ pcmpistri(ranges_ctrl, xmm1, xmm2);
//    //__ movdqu(xmm3, Operand(rax, 0));
//    //__ setcc(below_equal, scratch1);
//    //__ or_(scratch1, scratch2);
//    //__ j(zero, &simd_loop);
//
//    //// Get the index of the first match.
//    //__ cmpq(rcx, rbx);
//    //__ cmovq(below, rcx, rbx);
//
//    //__ subq(rax, Immediate(0x10));
//    //__ movq(string_pointer, rax);
//
//    //__ cmpq(rcx, Immediate(0x10));
//    //__ j(not_equal, &match);
//    //// Find the null char.
//    //Label find_null;
//    //__ dec_c(string_pointer);
//    //__ bind(&find_null);
//    //__ inc_c(string_pointer);
//    //__ movb(rax, Operand(string_pointer, 0));
//    //__ cmpb_al(Immediate(0));
//    //__ j(not_zero, &find_null);
//    //__ jmp(&exit);
//
//  } else {
    Label loop, match, exit;
    bool non_matching = bracket->flags() & Bracket::non_matching;
    Label* on_matching_char = non_matching ? &loop : &match;

    __ dec_c(string_pointer);
    __ bind(&loop);
    __ inc_c(string_pointer);
    MatchBracket(masm_, current_char, bracket, on_matching_char, &exit);
    if (non_matching) {
      __ jmp(&match);
    }
    __ jmp(&loop);

    __ bind(&match);
    FoundState(0, bracket->entry_state());
    __ bind(&exit);
  //}
}


void FastForwardGen::VisitSingleStartOrEndOfLine(ControlRegexp* seol) {
  ASSERT(seol->IsStartOfLine() || seol->IsEndOfLine());
  bool find_sol = seol->IsStartOfLine();
  if (CpuFeatures::IsAvailable(SSE4_2)) {
    Label align, align_loop;
    Label simd_code, simd_loop;
    Label adjust, simd_adjust, found, exit;

    if (find_sol) {
      __ cmpq(string_pointer, string_base);
      __ j(equal, &found);
      // TODO: Explain!
      // The loop below finds new line characters at string_pointer, and
      // string_pointer is corrected upon match.
      // So in practice it matches for sol at string_pointer + 1, and we must
      // not forget to check for sol at the current string_pointer position.
      MatchStartOrEndOfLine(masm_, seol, previous_char, &found);
    }

    // Align the string_pointer on a 16-bytes boundary for the SIMD loop.
    __ bind(&align);
    __ movb(rcx, string_pointer);
    __ negb(rcx);
    __ andb(rcx, Immediate(0xf));
    __ j(zero, &simd_code);

    __ movb(rax, current_char);

    __ bind(&align_loop);
    MatchStartOrEndOfLine(masm_, seol, current_char, &adjust, true);
    __ inc_c(string_pointer);
    __ movb(rax, current_char);
    if (find_sol) {
      __ cmpb_al(Immediate(0));
      __ loop(not_zero, &align_loop);
      // Exit on eos.
      __ j(zero, &exit);
    } else {
      __ loop(&align_loop);
    }


    __ bind(&simd_code);
    Label offset_0x0, offset_0x10, offset_0x20;
    static const uint8_t pcmp_str_control =
      Assembler::unsigned_bytes | Assembler::equal_any |
      Assembler::pol_pos | Assembler::lsi;

    __ movdq(xmm0, 0, '\n' << 8 | '\r');
    __ movdqa(xmm1, Operand(string_pointer, 0));

    __ bind(&simd_loop);
    __ pcmpistri(pcmp_str_control, xmm0, xmm1);
    __ j(below_equal, &offset_0x0);
    __ movdqa(xmm2, Operand(string_pointer, 0x10));
    __ pcmpistri(pcmp_str_control, xmm0, xmm2);
    __ j(below_equal, &offset_0x10);
    __ movdqa(xmm3, Operand(string_pointer, 0x20));
    __ pcmpistri(pcmp_str_control, xmm0, xmm3);
    __ j(below_equal, &offset_0x20);
    __ movdqa(xmm1, Operand(string_pointer, 0x30));
    __ addq(string_pointer, Immediate(0x30));
    __ jmp(&simd_loop);

    __ bind(&offset_0x20);
    __ addq(string_pointer, Immediate(0x10));
    __ bind(&offset_0x10);
    __ addq(string_pointer, Immediate(0x10));
    __ bind(&offset_0x0);
    __ cmpq(rcx, Immediate(0x10));
    __ j(not_equal, &simd_adjust);

    __ AdvanceToEOS();
    __ jmp(find_sol ? &exit : &found);

    __ bind(&simd_adjust);
    __ addq(string_pointer, rcx);
    __ bind(&adjust);
    if (find_sol) {
      // The string pointer should point after the new line.
      __ inc_c(string_pointer);
    }

    __ bind(&found);
    FoundState(0, seol->entry_state());
    __ bind(&exit);

  } else {
    Label loop, match, eos, done;
    const Operand& cchar =
      seol->IsStartOfLine() ? previous_char : current_char;

    if (find_sol) {
      // Check if we are at the beginning of the string.
      __ cmpq(string_pointer, string_base);
      __ j(equal, &match);
    }

    __ movb(rax, cchar);

    __ bind(&loop);
    MatchStartOrEndOfLine(masm_, seol, cchar, &match, true);
    __ inc_c(string_pointer);
    __ movb(rax, cchar);
    if (find_sol) {
      __ cmpb_al(Immediate(0));
      __ j(zero, &eos);
    }
    __ jmp(&loop);

    __ bind(&match);
    FoundState(0, seol->entry_state());
    if (find_sol) {
      // If we found the eos, we need to correct string_pointer to point to it
      // and not past it.
      __ jmp(&done);
      __ bind(&eos);
      __ dec_c(string_pointer);
    }
    __ bind(&done);
  }
}


void FastForwardGen::VisitSingleStartOfLine(StartOfLine* sol) {
  VisitSingleStartOrEndOfLine(sol);
}


void FastForwardGen::VisitSingleEndOfLine(EndOfLine* eol) {
  VisitSingleStartOrEndOfLine(eol);
}


void FastForwardGen::VisitSingleEpsilon(Epsilon* epsilon) {
  UNREACHABLE();
}


// Generic visitors --------------------------------------------------

// TODO: Benchmark those and optimize.

void FastForwardGen::VisitMultipleChar(MultipleChar* mc) {
  Label no_match;
  __ cmp_truncated(mc->chars_length(), current_chars, mc->first_chars());
  __ j(not_equal, &no_match);
  FoundState(0, mc->entry_state());
  __ jmp(potential_match_);
  __ bind(&no_match);
}


void FastForwardGen::VisitPeriod(Period* period) {
  Label no_match;
  __ cmpb(current_char, Immediate('\n'));
  __ j(equal, &no_match);
  __ cmpb(current_char, Immediate('\r'));
  __ j(equal, &no_match);
  FoundState(0, period->entry_state());
  __ jmp(potential_match_);
  __ bind(&no_match);
}


void FastForwardGen::VisitBracket(Bracket* bracket) {
  Label maybe_match, no_match;

  bool matching_bracket = !(bracket->flags() & Bracket::non_matching);
  Label* on_matching_char = matching_bracket ? &maybe_match : &no_match;

  MatchBracket(masm_, current_char, bracket, on_matching_char);
  if (matching_bracket) {
    __ jmp(&no_match);
  }

  __ bind(&maybe_match);
  FoundState(0, bracket->entry_state());
  __ jmp(potential_match_);
  __ bind(&no_match);
}


void FastForwardGen::VisitStartOfLine(StartOfLine* sol) {
  STATIC_ASSERT('\0' < '\n' && '\n' < '\r');
  Label maybe_match, no_match;
  __ cmpq(string_pointer, string_base);
  __ j(equal, &maybe_match);
  __ cmpb(previous_char, Immediate('\r'));
  __ j(above, &no_match);

  __ bind(&maybe_match);
  FoundState(0, sol->entry_state());
  __ jmp(potential_match_);
  __ bind(&no_match);
}


void FastForwardGen::VisitEndOfLine(EndOfLine* eol) {
  STATIC_ASSERT('\0' < '\n' && '\n' < '\r');
  Label no_match;
  __ cmpb(current_char, Immediate('\r'));
  __ j(above, &no_match);
  FoundState(0, eol->entry_state());
  __ jmp(potential_match_);
  __ bind(&no_match);
}


void FastForwardGen::VisitEpsilon(Epsilon* epsilon) {
  UNREACHABLE();
}


} }  // namespace rejit::internal

