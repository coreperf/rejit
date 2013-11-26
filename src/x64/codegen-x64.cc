// Copyright (C) 2013 Alexandre Rames <alexandre@coreperf.com>
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


int Codegen::TimeSummaryBaseOffsetFromFrame() {
  return -kCalleeSavedRegsSize - kStateInfoSize - time_summary_size();
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
  // TODO: Use a loop instruction when the time summary is big.
  __ Move(scratch, 0);
  for (int offset = 0; offset < time_summary_size() ; offset += kPointerSize) {
    __ or_(scratch, TimeSummary(offset));
  }
}


// This is the entry point from C++.
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
  __ testq(rdi, rdi);
  __ debug_msg(zero, "base string is NULL.\n");
  __ j(zero, &unwind_and_return);

  // Check the match results pointer.
  if (match_type != kMatchFull && !FLAG_benchtest) {
    __ testq(rdx, rdx);
    __ debug_msg(zero, "match results pointer is NULL.\n");
    __ j(zero, &unwind_and_return);
  }


  //  0x8 and up     : callee saved registers
  //  0x0            : rbp caller's rbp.
  // -kStateInfoSize : Saved state. See definition of kStateInfoSize for
  //                   comments.
  // -reserved_space : State ring.

  const size_t reserved_space =
    kStateInfoSize + state_ring_size() + time_summary_size();
  __ movq(scratch, rsp);
  __ subq(rsp, Immediate(reserved_space));
  __ ZeroMem(rsp, scratch);

  __ movq(string_pointer, rdi);
  __ Move(ring_index, 0);

  __ movq(string_base, rdi);
  __ addq(rsi, rdi);
  __ movq(string_end, rsi);
  __ movq(result_matches, rdx);
  __ movq(ff_position, rdi);
  // Adjust for the initial character offset in FF.
  __ subq(ff_position, Immediate(kCharSize));


  if (match_type == kMatchFull) {
    SetStateForce(0, rinfo->entry_state());
    GenerateMatchForward(rinfo, match_type);

  } else {
    Label fast_forward;

    __ bind(&fast_forward);
    if (FLAG_use_fast_forward &&
        GenerateFastForward(rinfo, match_type)) {
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

  }

  // Unwind the stack and return.
  __ cld();
  __ bind(&unwind_and_return);
  __ addq(rsp, Immediate(reserved_space));
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

      // TODO: This is wrong. backward matching should share most of the code
      // for non match-full types.
      // matchanywhere can only do this optimization of exiting early on
      // backward if there is only one ff-element.
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

  __ cmpq(string_pointer, direction == kForward ? string_end : string_base);
  __ j(equal, &limit);

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
        Label keep_searching;
        __ j(not_zero, &keep_searching);

        // TODO: Same as other TODO below. Why do we need to check one character
        // ahead??
        __ movq(scratch, string_pointer);
        __ incq(scratch);
        __ cmpq(scratch, string_end);
        __ j(above_equal, &limit);
        //if (direction != kForward) {
        //  // For forward matches this would be redundant with the check before
        //  // CheckTimeFlow().
        //  __ cmpq(string_pointer, string_end);
        //  __ j(equal, &limit);
        //}
        //// TODO: Comment on this. Why do we need it?
        ////        First test failing without this: line 115
        //__ cmpb(next_char, Immediate(0));
        //__ j(zero, &limit);

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
            __ testq(match, match);
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
            __ jmp(fast_forward);
          }
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

  // Generate code to match the regexps.
  // Skip regexps which entry state is unset.
  if (direction == kForward) {
    sort(gen_list->begin(), gen_list->end(), &regexp_cmp_entry_state);
  } else {
    sort(gen_list->begin(), gen_list->end(), &regexp_cmp_output_state);
  }
  Label skip;
  int current_state = -1;
  for (it = gen_list->begin(); it < gen_list->end(); it++) {
    // TODO: No need to generate code for control regexps?
    if ((direction == kForward && (*it)->entry_state() != current_state) ||
        (direction == kBackward && (*it)->output_state() != current_state)) {
      __ bind(&skip);
      skip.Unuse();
      current_state =
        direction == kForward ? (*it)->entry_state() : (*it)->output_state();
      TestState(0, current_state);
      __ j(zero, &skip);
    }
    // Control regexps are handled in HandleControlRegexps().
    Visit(*it);
  }
  __ bind(&skip);

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
        __ testq(match, match);
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
        __ testq(match, match);
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
        __ cmpq(scratch, string_end);
        __ j(equal, &exit);
        if (fast_forward) {
          __ jmp(fast_forward);
        }

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
  if (on_start_of_string && seol->IsStartOfLine()) {
    ASSERT(seol->IsStartOfLine());
    __ cmpq(string_pointer, string_base);
    __ j(equal, on_start_of_string);
  }
  if (seol->IsEndOfLine()) {
    __ cmpq(string_pointer, string_end);
    __ j(equal, on_match);
  }
  if (!char_loaded_in_rax) {
    __ movb(rax, char_pos);
  }
  __ cmpb_al(Immediate('\n'));
  __ j(equal, on_match);
  __ cmpb_al(Immediate('\r'));
  __ j(equal, on_match);
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


static void CheckEnoughStringLength(MacroAssembler *masm_,
                                    Direction direction,
                                    unsigned n_bytes,
                                    Label * on_not_enough) {
  __ movq(scratch, string_pointer);
  if (direction == kForward) {
    __ addq(scratch, Immediate(n_bytes));
    __ cmpq(scratch, string_end);
    __ j(above, on_not_enough);
  } else {
    __ subq(scratch, Immediate(n_bytes));
    __ cmpq(scratch, string_base);
    __ j(below, on_not_enough);
  }
}
// Try to match mc from the current string_pointer position.
// string_pointer is not modified.
// On output the condition flags will match equal/not_equal depending on wether
// there is a match or not.
// If provided, fixed_chars contains the min(8, n_chars) first bytes of the mc.
static void MatchMultipleChar(MacroAssembler *masm_,
                              Direction direction,
                              MultipleChar* mc,
                              bool eos_safe = false,
                              Label* on_no_match = NULL,
                              Register fixed_chars = no_reg) {
  Label done;
  unsigned n_chars = mc->chars_length();

  // TODO: Implement a SIMD path.

  if (!eos_safe) {
    CheckEnoughStringLength(masm_, direction, n_chars,
                            on_no_match ? on_no_match : &done);
  }

  const Operand c = direction == kForward ?
    current_chars : Operand(string_pointer, -(n_chars - 1));

  if (!fixed_chars.is_valid()) {
    __ cmp_truncated(n_chars, c, mc->imm_chars());
  } else {
    __ cmp_truncated(n_chars, fixed_chars, c);
  }
  if (on_no_match) {
    __ j(not_equal, on_no_match);
  } else if (n_chars > 8) {
    __ j(not_equal, &done);
  }


  // Avoid performing the same check as above.
  // Also don't perform a check for long strings: the pre-check above was enough
  // and we can transition to the full check below.
  if (n_chars < 8 && !IsPowerOf2(n_chars)) {
    if (eos_safe) {
      if (!fixed_chars.is_valid()) {
        __ cmp(n_chars, c, mc->imm_chars());
      } else {
        __ cmp(n_chars, c, fixed_chars);
      }
    } else {
      if (!fixed_chars.is_valid()) {
        __ cmp_safe(n_chars, equal, c, mc->imm_chars(), on_no_match ? on_no_match : &done);
      } else {
        __ cmp_safe(n_chars, equal, c, fixed_chars, on_no_match ? on_no_match : &done);
      }
    }
    if (on_no_match) {
      __ j(not_equal, on_no_match);
    } else if (n_chars > 8) {
      __ j(not_equal, &done);
    }
  }

  if (n_chars > 8) {
    __ movq(rsi, string_pointer);
    if (direction == kForward) {
      __ Move(rdi, (uint64_t)(mc->chars()));
    } else {
      __ Move(rdi, (uint64_t)(mc->chars() + n_chars - 1));
    }
    __ Move(rcx, n_chars / 8);
    __ repnecmpsq();
    if (n_chars % 8 > 0) {
      __ Move(rcx, n_chars % 8);
      __ repnecmpsb();
    }
    if (on_no_match) {
      __ j(not_equal, on_no_match);
    }
  }
  __ bind(&done);
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

  MatchMultipleChar(masm_, direction(), mc, false, &no_match);

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
  if (on_eos) {
    __ cmpq(string_pointer, string_end);
    __ j(equal, on_eos);
  }

  __ movb(rax, c);

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
               bracket, on_matching_char, &no_match);
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

    Label align_or_finish;
    Label simd_code, standard_code;
    Label maybe_match;
    Label potential_match;
    Label exit;

    vector<Regexp*>::iterator it;
    bool multiple_chars_only = true;
    for (it = regexp_list_->begin(); it < regexp_list_->end(); it++) {
      if (!(*it)->IsMultipleChar()) {
        multiple_chars_only = false;
        break;
      }
    }

    // We currently only support a SIMD path for alternations of MultipleChars.
    // TODO: Add support for alternations of other regexps. This should be
    // simpler with the new code structure.
    if (CpuFeatures::IsAvailable(SSE4_2) &&
        multiple_chars_only &&
        // xmm0-xmm8 give 8 registers minus one allocated for the string.
        // TODO: Can we use a REX prefix to use all xmm registers?
        regexp_list_->size() <= 7) {
      // This code is designed after VisitSingleMultipleChar().

      static const uint8_t pcmp_str_control =
        Assembler::unsigned_bytes | Assembler::equal_order |
        Assembler::pol_pos | Assembler::lsi;

      // Pre-load the XMM registers for MultipleChars.
      static const int first_free_xmm_code = 1;
      unsigned min_n_chars = kMaxNodeLength, max_n_chars = 0;
      for (unsigned i = 0; i < regexp_list_->size(); i++) {
        MultipleChar *mc = regexp_list_->at(i)->AsMultipleChar();
        __ movdqp(XMMRegister::from_code(first_free_xmm_code + i),
                  mc->chars(), mc->chars_length());
        min_n_chars = min(min_n_chars, mc->chars_length());
        max_n_chars = max(max_n_chars, mc->chars_length());
      }


      Register low_index = scratch2;
      Register simd_max_index = scratch3;
      unsigned margin_for_simd_loop = 0xf + 0x40 + 0x8;
      unsigned margin_before_eos = max(min_n_chars, margin_for_simd_loop);
      __ movq(simd_max_index, string_end);
      __ subq(simd_max_index, Immediate(margin_before_eos));

      __ bind(&align_or_finish);
      __ cmpq(string_pointer, simd_max_index);
      __ j(above, &standard_code);

      __ movdqu(xmm0, Operand(string_pointer, 0x0));
      __ Move(low_index, 0x10);
      for (unsigned i = 0; i < regexp_list_->size(); i++) {
        __ pcmpistri(pcmp_str_control,
                     XMMRegister::from_code(first_free_xmm_code + i),
                     xmm0);
        __ cmpq(rcx, low_index);
        __ cmovq(below, low_index, rcx);
      }
      __ cmpq(low_index, Immediate(0x10));
      __ j(not_equal, &maybe_match);

      __ and_(string_pointer, Immediate(~0xf));
      __ addq(string_pointer, Immediate(0x10));

      __ bind(&simd_code);

      Label match_somewhere_0x00, match_somewhere_0x10,
            match_somewhere_0x20, match_somewhere_0x30;

      // This loop scans the code for eos or potential match.
      // It makes no distinction between potential matches to be able to scan as
      // fast as possible. When a potential match is detected, it hands control
      // to a more thorough code.
      Label simd_loop;
      __ bind(&simd_loop);
      __ cmpq(string_pointer, simd_max_index);
      __ j(above, &standard_code);
#define fast_round(current_offset)                                             \
      for (unsigned i = 0; i < regexp_list_->size(); i++) {                    \
        if (i == 0) {                                                          \
          /* The conditional jump above ensures that the eos wasn't reached. */\
          __ movdqa(xmm0,                                                      \
                    Operand(string_pointer, current_offset));                  \
        }                                                                      \
        __ pcmpistri(pcmp_str_control,                                         \
                     XMMRegister::from_code(first_free_xmm_code + i),          \
                     xmm0);                                                    \
        __ j(below, &match_somewhere_##current_offset);                        \
      }
      // TODO: Do we need so many rounds?
      fast_round(0x00)
      fast_round(0x10)
      fast_round(0x20)
      fast_round(0x30)

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
      __ Move(low_index, 0x10);

      __ movdqa(xmm0, Operand(string_pointer, 0));
      for (unsigned i = 0; i < regexp_list_->size(); i++) {
        __ pcmpistri(pcmp_str_control,
                     XMMRegister::from_code(first_free_xmm_code + i), xmm0);
        __ cmpq(rcx, low_index);
        __ cmovq(below, low_index, rcx);
      }

      __ bind(&maybe_match);
      // We may have a potential match.
      // Check if it is good enough to exit the fast forward loop.
      __ addq(string_pointer, low_index);
      for (unsigned i = 0; i < regexp_list_->size(); i++) {
        // TODO: We could use pre-loaded registers if there are enough. Or maybe
        // move from xmm registers if it is faster.
        Label no_match;
        MultipleChar *mc = regexp_list_->at(i)->AsMultipleChar();
        MatchMultipleChar(masm_, kForward, mc, true, &no_match);
        FoundState(0, mc->entry_state());
        __ jmp(&exit);
        __ bind(&no_match);
      }
      __ jmp(&align_or_finish);
    }

    __ bind(&standard_code);

    Label loop;
    potential_match_ = &potential_match;

    __ dec_c(string_pointer);
    __ bind(&loop);
    __ inc_c(string_pointer);
    for (it = regexp_list_->begin(); it < regexp_list_->end(); it++) {
      Visit(*it);
    }
    // We must check for eos after having visited the ff elements, because eos
    // may be a potential match for one of them.
    // TODO: We could handle that by testing those for which it can be a
    // potential match before this and others after.
    __ cmpq(string_pointer, string_end);
    __ j(not_equal, &loop);

    __ bind(&potential_match);
    __ bind(&exit);
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

  Label found, exit;
  Label standard_code;

  Register simd_max_index = scratch2;
  Register fixed_chars = scratch3;
  XMMRegister fixed_chars_simd = xmm0;

  // Pre-load the constant values for the characters to match.
  __ MoveCharsFrom(fixed_chars, n_chars, mc->chars());
  if (CpuFeatures::IsAvailable(SSE4_2)) {
    __ movdqp(fixed_chars_simd, mc->chars(), n_chars);
  }


  if (CpuFeatures::IsAvailable(SSE4_2)) {
    Label align_or_finish;
    Label simd_code, simd_loop;
    Label potential_match;

    static const uint8_t pcmp_str_control =
      Assembler::unsigned_bytes | Assembler::equal_order |
      Assembler::pol_pos | Assembler::lsi;

    // Only execute the SIMD code if the length of string to process is big
    // enough to be aligned on a 0x10 bytes boundary (maximum 0xf offset
    // adjustment), go through one iteration of the SIMD loop, and allow for a
    // 'eos-safe' accesses in MatchMultipleChar.
    // If the length of the mc is greater than that we can even stop earlier.
    int margin_for_simd_loop = 0xf + 0x20 + 0x8;
    int margin_before_eos = max(n_chars, margin_for_simd_loop);
    __ movq(simd_max_index, string_end);
    __ subq(simd_max_index, Immediate(margin_before_eos));

    __ bind(&align_or_finish);
    __ cmpq(string_pointer, simd_max_index);
    __ j(above, &standard_code);

    // We know there are more than 0x10 bytes to process, so we can safely use
    // movdqu and pcmpistri.
    // Note that we check further than is actually required to align
    // string_pointer, but there is no point purposedly ignoring a match.
    __ movdqu(xmm1, Operand(string_pointer, 0x0));
    __ pcmpistri(pcmp_str_control, xmm0, xmm1);
    // If CFlag is set there was a match.
    __ j(below, &potential_match);

    // No match in the following 0x10 bytes. Align the string pointer on the
    // next closest 0x10 bytes boundary.
    __ and_(string_pointer, Immediate(~0xf));
    __ addq(string_pointer, Immediate(0x10));


    __ bind(&simd_code);
    Label offset_0x0, offset_0x10;
    // TODO: Would pcmpEstr be faster than pcmpIstr?
    // TODO: Could use pcmpistrM followed by bsr/bsf to free rcx to be used with
    // a loop instruction.
    __ bind(&simd_loop);
    __ cmpq(string_pointer, simd_max_index);
    __ j(above, &standard_code);
    __ movdqa(xmm1, Operand(string_pointer, 0x0));
    __ pcmpistri(pcmp_str_control, xmm0, xmm1);
    __ j(below, &offset_0x0);
    __ movdqa(xmm2, Operand(string_pointer, 0x10));
    __ pcmpistri(pcmp_str_control, xmm0, xmm2);
    __ j(below, &offset_0x10);
    __ addq(string_pointer, Immediate(0x20));
    __ jmp(&simd_loop);

    __ bind(&offset_0x10);
    __ addq(string_pointer, Immediate(0x10));
    __ bind(&offset_0x0);

    __ bind(&potential_match);
    // After pcmpistri rcx contains the offset to the first potential match.
    __ addq(string_pointer, rcx);
    MatchMultipleChar(masm_, kForward, mc, true, &align_or_finish, fixed_chars);

    __ jmp(&found);
  }

  __ bind(&standard_code);
  // The standard code is used when SIMD is not available or when the length of
  // string left to process is too small for the SIMD loop.

  Label loop;

  // By stopping early we avoid useless processing and ensure we are not
  // accessing memory from the eos.
  __ movq(scratch2, string_end);
  __ subq(scratch2, Immediate(n_chars));

  __ dec_c(string_pointer);
  __ bind(&loop);
  __ inc_c(string_pointer);
  __ cmpq(string_pointer, scratch2);
  __ j(above, &exit);
  __ cmp_truncated(n_chars, fixed_chars, current_chars);
  __ j(not_equal, &loop);

  __ bind(&found);
  FoundState(0, mc->entry_state());
  __ bind(&exit);
}


// TODO(rames): slow single visitors are all too similar not to be refactord!
void FastForwardGen::VisitSinglePeriod(Period* period) {
  // TODO(rames): we probably never want to have a single ff for a period!!
  // Need to refactor the ff finder mechanisms.
  Label loop, done;

  __ dec_c(string_pointer);

  __ bind(&loop);
  __ inc_c(string_pointer);

  __ cmpq(string_pointer, string_end);
  __ j(equal, &done);

  __ movb(rax, current_char);
  __ cmpb_al(Immediate('\n'));
  __ j(equal, &loop);
  __ cmpb_al(Immediate('\r'));
  __ j(equal, &loop);

  __ bind(&done);
  FoundState(0, period->entry_state());
}


void FastForwardGen::VisitSingleBracket(Bracket* bracket) {
  bool non_matching = bracket->flags() & Bracket::non_matching;

  Label standard_code;
  Label match;
  Label exit;

  // This code is inefficient. The high density of matches makes the SIMD setup
  // too costly.
  //if (CpuFeatures::IsAvailable(SSE4_2) &&
  //    bracket->single_chars()->size() < 16 &&
  //    bracket->char_ranges()->size() < 8) {
  //  Label align_or_finish;
  //  Label simd_code, simd_loop;
  //  Label simd_adjust;

  //  const uint64_t ctrl_single_chars =
  //    Assembler::unsigned_bytes | Assembler::equal_any |
  //    Assembler::pol_pos | Assembler::lsi;
  //  const uint64_t ctrl_ranges =
  //    Assembler::unsigned_bytes | Assembler::ranges |
  //    Assembler::pol_pos | Assembler::lsi;

  //  // Only execute the SIMD code if the length of string to process is big
  //  // enough to be aligned on a 0x10 bytes boundary (maximum 0xf offset
  //  // adjustment) and go through one iteration of the SIMD loop.
  //  Register simd_max_index = scratch3;
  //  int margin_for_simd_loop = 0xf + 0x20;
  //  __ movq(simd_max_index, string_end);
  //  __ subq(simd_max_index, Immediate(margin_for_simd_loop));

  //  // Preload the xmm registers.
  //  XMMRegister xmm_chars = xmm1;
  //  XMMRegister xmm_ranges = xmm2;
  //  uint64_t high = 0;
  //  uint64_t low = 0;
  //  if (bracket->single_chars()->size()) {
  //    for (unsigned i = 0; i < bracket->single_chars()->size() && i < 8; i++) {
  //      low |= bracket->single_chars()->at(i) << (i * 8);
  //    }
  //    for (unsigned i = 8; i < bracket->single_chars()->size() && i < 16; i++) {
  //      high |= bracket->single_chars()->at(i) << ((i - 8) * 8);
  //    }
  //    __ movdq(xmm_chars, high, low);
  //  }
  //  if (bracket->char_ranges()->size()) {
  //    for (unsigned i = 0; i < bracket->char_ranges()->size() && i < 4; i++) {
  //      low |= bracket->char_ranges()->at(i).low << (i * 8);
  //      low |= bracket->char_ranges()->at(i).high << (i * 8 + 8);
  //    }
  //    for (unsigned i = 4; i < bracket->char_ranges()->size() && i < 8; i++) {
  //      high |= bracket->char_ranges()->at(i).low << ((i - 4) * 8);
  //      high |= bracket->char_ranges()->at(i).high << ((i - 4) * 8 + 8);
  //    }
  //    __ movdq(xmm_ranges, high, low);
  //  }

  //  __ bind(&align_or_finish);
  //  __ cmpq(string_pointer, simd_max_index);
  //  __ j(above, &standard_code);

  //  // We know there are more than 0x10 bytes to process, so we can safely use
  //  // movdqu and pcmpistri.
  //  // Note that we check further than is actually required to align
  //  // string_pointer, but there is no point purposedly ignoring a match.
  //  __ movdqu(xmm2, Operand(string_pointer, 0x0));
  //  if (bracket->single_chars()->size()) {
  //    __ pcmpistri(ctrl_single_chars, xmm_chars, xmm2);
  //    __ j(below, &match);
  //  }
  //  if (bracket->char_ranges()->size()) {
  //    __ pcmpistri(ctrl_ranges, xmm_ranges, xmm2);
  //    __ j(below, &match);
  //  }

  //  // No match in the following 0x10 bytes. Align the string pointer on the
  //  // next closest 0x10 bytes boundary.
  //  __ and_(string_pointer, Immediate(~0xf));
  //  __ addq(string_pointer, Immediate(0x10));


  //  __ bind(&simd_code);
  //  Label offset_0x0, offset_0x10;
  //  __ bind(&simd_loop);
  //  __ cmpq(string_pointer, simd_max_index);
  //  __ j(above, &standard_code);
  //  __ movdqa(xmm3, Operand(string_pointer, 0x0));
  //  if (bracket->single_chars()->size()) {
  //    __ pcmpistri(ctrl_single_chars, xmm_chars, xmm3);
  //    __ j(below, &offset_0x0);
  //  }
  //  if (bracket->char_ranges()->size()) {
  //    __ pcmpistri(ctrl_ranges, xmm_ranges, xmm3);
  //    __ j(below, &offset_0x0);
  //  }
  //  __ movdqa(xmm4, Operand(string_pointer, 0x10));
  //  if (bracket->single_chars()->size()) {
  //    __ pcmpistri(ctrl_single_chars, xmm_chars, xmm4);
  //    __ j(below, &offset_0x10);
  //  }
  //  if (bracket->char_ranges()->size()) {
  //    __ pcmpistri(ctrl_ranges, xmm_ranges, xmm4);
  //    __ j(below, &offset_0x10);
  //  }
  //  __ addq(string_pointer, Immediate(0x20));
  //  __ jmp(&simd_loop);

  //  __ bind(&offset_0x10);
  //  __ addq(string_pointer, Immediate(0x10));
  //  __ bind(&offset_0x0);

  //  __ bind(&simd_adjust);
  //  // After pcmpistri rcx contains the offset to the first potential match.
  //  __ addq(string_pointer, rcx);
  //  __ jmp(&match);
  //}

  __ bind(&standard_code);

  Label loop;
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
}


void FastForwardGen::VisitSingleStartOrEndOfLine(ControlRegexp* seol) {
  ASSERT(seol->IsStartOfLine() || seol->IsEndOfLine());
  bool sol = seol->IsStartOfLine();

  Label standard_code;
  Label adjust_match, match, exit;

  if (sol) {
    // The loop below finds new line characters at string_pointer, and
    // string_pointer is corrected upon match.
    // So in practice it matches for sol at string_pointer + 1, and we must
    // not forget to check for sol at the current string_pointer position.
    MatchStartOrEndOfLine(masm_, seol, previous_char, &match, false, &match);
  }

  if (CpuFeatures::IsAvailable(SSE4_2)) {
    // This SIMD code is designed after VisitSingleMultipleChar().
    Label align_or_finish;
    Label simd_code, simd_loop;
    Label simd_adjust;

    static const uint8_t pcmp_str_control =
      Assembler::unsigned_bytes | Assembler::equal_any |
      Assembler::pol_pos | Assembler::lsi;

    __ movdq(xmm0, 0, ('\r' << 8) | '\n');

    // Only execute the SIMD code if the length of string to process is big
    // enough to be aligned on a 0x10 bytes boundary (maximum 0xf offset
    // adjustment) and go through one iteration of the SIMD loop.
    Register simd_max_index = scratch3;
    int margin_for_simd_loop = 0xf + 0x20;
    __ movq(simd_max_index, string_end);
    __ subq(simd_max_index, Immediate(margin_for_simd_loop));

    __ bind(&align_or_finish);
    __ cmpq(string_pointer, simd_max_index);
    __ j(above, &standard_code);

    // We know there are more than 0x10 bytes to process, so we can safely use
    // movdqu and pcmpistri.
    // Note that we check further than is actually required to align
    // string_pointer, but there is no point purposedly ignoring a match.
    __ movdqu(xmm1, Operand(string_pointer, 0x0));
    __ pcmpistri(pcmp_str_control, xmm0, xmm1);
    // If CFlag is set there was a match.
    __ j(below, &simd_adjust);

    // No match in the following 0x10 bytes. Align the string pointer on the
    // next closest 0x10 bytes boundary.
    __ and_(string_pointer, Immediate(~0xf));
    __ addq(string_pointer, Immediate(0x10));

    __ bind(&simd_code);
    Label offset_0x0, offset_0x10;
    __ bind(&simd_loop);
    __ cmpq(string_pointer, simd_max_index);
    __ j(above, &standard_code);
    __ movdqa(xmm1, Operand(string_pointer, 0x0));
    __ pcmpistri(pcmp_str_control, xmm0, xmm1);
    __ j(below, &offset_0x0);
    __ movdqa(xmm2, Operand(string_pointer, 0x10));
    __ pcmpistri(pcmp_str_control, xmm0, xmm2);
    __ j(below, &offset_0x10);
    __ addq(string_pointer, Immediate(0x20));
    __ jmp(&simd_loop);

    __ bind(&offset_0x10);
    __ addq(string_pointer, Immediate(0x10));
    __ bind(&offset_0x0);

    __ bind(&simd_adjust);
    // After pcmpistri rcx contains the offset to the first potential match.
    __ addq(string_pointer, rcx);
    __ jmp(&adjust_match);
  }

  __ bind(&standard_code);

  __ movq(rcx, string_end);
  __ subq(rcx, string_pointer);
  if (sol) {
    __ j(equal, &exit);
  } else {
    __ j(equal, &match);
  }

  Label loop;
  __ bind(&loop);
  __ movb(rax, current_char);
  __ cmpb_al(Immediate('\n'));
  __ j(equal, &adjust_match);
  __ cmpb_al(Immediate('\r'));
  __ j(equal, &adjust_match);
  __ inc_c(string_pointer);
  __ loop(&loop);

  if (sol) {
    __ jmp(&exit);
  }

  __ bind(&adjust_match);
  if (sol) {
    // Adjust the string pointer.
    __ inc_c(string_pointer);
  }
  __ bind(&match);
  FoundState(0, seol->entry_state());
  __ bind(&exit);
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
  MatchMultipleChar(masm_, kForward, mc, false, &no_match);
  FoundState(0, mc->entry_state());
  __ jmp(potential_match_);
  __ bind(&no_match);
}


void FastForwardGen::VisitPeriod(Period* period) {
  Label no_match;
  __ cmpq(string_pointer, string_end);
  __ j(equal, &no_match);
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

  MatchBracket(masm_, current_char, bracket, on_matching_char, &no_match);
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
  Label maybe_match, no_match;
  __ cmpq(string_pointer, string_end);
  __ j(equal, &maybe_match);
  __ cmpb(current_char, Immediate('\r'));
  __ j(above, &no_match);
  __ bind(&maybe_match);
  FoundState(0, eol->entry_state());
  __ jmp(potential_match_);
  __ bind(&no_match);
}


void FastForwardGen::VisitEpsilon(Epsilon* epsilon) {
  UNREACHABLE();
}


} }  // namespace rejit::internal

