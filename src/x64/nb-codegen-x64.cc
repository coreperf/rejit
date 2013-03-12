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

#include "nb-codegen.h"
#include "x64/macro-assembler-x64.h"
#include <cmath>


namespace rejit {
namespace internal {


#define __ masm_->

NB_Codegen::NB_Codegen()
  : masm_(new MacroAssembler()),
    ring_base_(rax, 0) {}


const int kStackSavedInfo = 8 * kPointerSize;


int NB_Codegen::TimeSummaryBaseOffsetFromFrame() {
  return -kStackSavedInfo - time_summary_size();
}


Operand NB_Codegen::TimeSummary(int offset) {
  return Operand(rbp, TimeSummaryBaseOffsetFromFrame() + offset);
}


Operand NB_Codegen::TimeSummaryOperand(int time) {
  return Operand(rbp, TimeSummaryBaseOffsetFromFrame() + time / kBitsPerByte);
}


Operand NB_Codegen::StateRingBase() {
  return Operand(rbp, StateRingBaseOffsetFromFrame());
}


int NB_Codegen::StateRingBaseOffsetFromFrame() {
  return TimeSummaryBaseOffsetFromFrame() - state_ring_size();
}


Operand NB_Codegen::StateOperand(Register offset) {
  return Operand(rbp, offset, times_1, StateRingBaseOffsetFromFrame());
}


Operand NB_Codegen::StateOperand(int time, int state_index) {
  return Operand(rbp,
                 ring_index,
                 times_1,
                 StateRingBaseOffsetFromFrame() +
                 time * state_ring_time_size() + state_index * kPointerSize);
}


Operand NB_Codegen::StateOperand(int time, Register state_index) {
  __ movq(scratch, state_index);
  // TODO(rames): abstract log2 kPointerSize.
  __ shl(scratch, Immediate(3));
  __ addq(scratch, ring_index);
  __ addq(scratch, Immediate(StateRingBaseOffsetFromFrame() + time *
                             state_ring_time_size()));
  return Operand(rbp,
                 scratch,
                 times_1,
                 0);
}


void NB_Codegen::ComputeStateOperandOffset(Register offset, int time, int index) {
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


Operand NB_Codegen::result_matches() {
  return Operand(rbp, -2 * kPointerSize);
}


Operand end_of_string() {
  return Operand(rbp, -3 * kPointerSize);
}


Operand ff_position() {
  return Operand(rbp, -4 * kPointerSize);
}


Operand ff_found_state() {
  return Operand(rbp, -5 * kPointerSize);
}


Operand backward_match() {
  return Operand(rbp, -6 * kPointerSize);
}


Operand forward_match() {
  return Operand(rbp, -7 * kPointerSize);
}


Operand last_match_end() {
  return Operand(rbp, -8 * kPointerSize);
}


void NB_Codegen::FlowTime() {
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


void NB_Codegen::CheckTimeFlow() {
  __ Move(scratch, 0);
  for (int offset = 0; offset < time_summary_size() ; offset += kPointerSize) {
    __ or_(scratch, TimeSummary(offset));
  }
}


void NB_Codegen::Generate(RegexpInfo* rinfo,
                          MatchType match_type) {
  if (!CpuFeatures::initialized()) {
    CpuFeatures::Probe();
  }

  Label unwind_and_return;
  Label next_character, null_char;

  match_type_ = match_type;

  vector<Regexp*>::const_iterator it;
  vector<Regexp*>* gen_list = rinfo->gen_list();

  if (FLAG_trace_re_list) {
    cout << "Regexp list ----------" << endl;
    for (it = gen_list->begin(); it < gen_list->end(); it++) {
      cout << **it << endl;
    }
    cout << "---------- End of regexp list" << endl;
  }

  // Setup the frame pointer.
  __ PreserveRegs();
  __ push(rbp);
  __ movq(rbp, rsp);

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

  // Reserve space on the stack for the ring of states and other structures.
  // rbp - 0                                    : caller's rbp.
  // rbp - 8                                    : string base.
  // rbp - 0x10                                 : match results or NULL.
  // rbp - 0x10 - time_summary_size             : time summary base.
  // rbp - 0x10 - time_summary_size - ring_size : state ring base.
  // TODO(rames): update description of stack.

  // Save the string base and the result matches passed as arguments from C++.
  __ push(rdi);   // String base.
  __ push(rsi);   // result matches.

  // Save space for the pointer to the end of the string.
  __ push(rdi);
  // Save space for the fast forward position.
  // Adjust for the initial 1 char offset from ff.
  __ movq(scratch, rdi);
  __ subq(scratch, Immediate(kCharSize));
  __ push(scratch);

  __ Move(scratch, 0);
  __ push(scratch);   // FF found state.
  __ push(scratch);   // backward match.
  __ push(scratch);   // forward match.
  __ push(scratch);   // Last match end.

  ASSERT(kStackSavedInfo == 8 * kPointerSize);

  __ movq(string_pointer, rdi);
  __ movq(ring_index, Immediate(0));

  __ movq(rax, rsp);
  __ subq(rax,
          Immediate(state_ring_size() + time_summary_size()));
  // Initialize this space to 0.
  // TODO(rames): Optimize this. use the 'loop' instruction?
  Label init_zero;
  __ bind(&init_zero);
  __ push(Immediate(0));
  __ cmpq(rsp, rax);
  __ j(not_equal, &init_zero);


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
  __ movq(rsp, rbp);
  __ pop(rbp);
  __ RestoreRegs();
  __ ret(0);
}


bool NB_Codegen::GenerateFastForward(RegexpInfo* rinfo,
                                     MatchType match_type) {
  vector<Regexp*>* fflist = rinfo->ff_list();
  FF_finder fff(rinfo->regexp(), fflist);
  bool ff_success = fff.Visit(rinfo->regexp());

  if (FLAG_trace_ff_finder) {
    Indent(cout) << "fast forward regexps {" << endl;
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
    Indent(cout) << "} end of fast forward regexps" << endl;
  }

  if (ff_success) {
    NB_FastForwardGen ffgen(this, rinfo->ff_list());
    __ movq(string_pointer, ff_position());
    __ addq(string_pointer, Immediate(kCharSize));
    // Clear the temporary matches.
    __ movq(backward_match(), Immediate(0));
    __ movq(forward_match(),  Immediate(0));
    __ movq(last_match_end(), Immediate(0));
    ffgen.Generate();
    __ movq(ff_position(), string_pointer);
  }
  return ff_success;
}


void NB_Codegen::GenerateFastForward_iter(RegexpInfo* rinfo,
                                          MatchType match_type) {
  ff_pre_scan_ = match_type == kMatchAll || FLAG_force_ff_pre_scan;
  if (ff_pre_scan()) {
    __ Move(rax, 0);
    __ movq(rdi, string_pointer);
    // There must be a terminating character '\0'.
    __ movq(rcx, Immediate(-1));
    __ repnescasb();
    // Point at the terminating character.
    __ decq(rdi);
    __ movq(end_of_string(), rdi);
  }
  NB_FastForwardGen_iter ffgen_iter(masm_, rinfo->gen_list(), ff_pre_scan());
  ffgen_iter.VisitEntryState(rinfo->entry_state());
}


void NB_Codegen::HandleControlRegexps(RegexpInfo* rinfo) {
  vector<Regexp*>::iterator it;
  for (it = rinfo->gen_list()->begin(); it < rinfo->gen_list()->end(); it++) {
    if ((*it)->IsControlRegexp()) {
      Visit(*it);
    }
  }
}


void NB_Codegen::CheckMatch(Direction direction,
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
        __ movq(scratch, ff_found_state());
        SetStateForce(0, scratch);
        __ movq(string_pointer, ff_position());
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
      Operand remember_match = direction == kBackward ? backward_match()
        : forward_match();
      int match_state =  direction == kBackward ? rinfo->entry_state()
        : rinfo->output_state();
      TestState(0, match_state);
      __ j(zero, &no_match);

      __ movq(remember_match, string_pointer);

      __ bind(&no_match);
      break;
    }

    case kMatchAll: {
      Label no_match;
      // A match is not an exit situation, as longer matches may occur.
      Operand remember_match = direction == kBackward ? backward_match()
        : forward_match();
      int match_state =  direction == kBackward ? rinfo->entry_state()
        : rinfo->output_state();

      if (direction == kBackward) {
        // A new match cannot start before the latest match finishes.
        __ cmpq(string_pointer, last_match_end());
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


void NB_Codegen::GenerateMatchDirection(Direction direction,
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
        __ cmpb(next_char, Immediate(0));
        __ j(zero, &limit);

        if (match_type == kMatchAnywhere) {
          __ jmp(fast_forward);

        } else if (match_type == kMatchFirst) {
          Operand remembered_match = direction == kBackward ? backward_match()
            : forward_match();
          __ cmpq(remembered_match, Immediate(0));
          __ j(zero, fast_forward);
          __ jmp(&limit);

        } else {  // kMatchAll
          Operand remembered_match = direction == kBackward ? backward_match()
            : forward_match();
          __ cmpq(remembered_match, Immediate(0));
          __ j(zero, fast_forward);


          if (direction == kBackward) {
            __ movq(string_pointer, backward_match());
            __ movq(scratch2, ff_found_state());
            SetStateForce(0, scratch2);
            __ movq(string_pointer, ff_position());
            __ jmp(&exit);

          } else {
            // TOTO(rames): Merge code with CheckMatch.
            Register match = rdi;
            __ movq(match, result_matches());
            __ cmpq(match, Immediate(0));
            __ j(zero, &keep_searching);
            __ movq(rdx, forward_match());
            __ movq(last_match_end(), rdx);
            __ movq(ff_position(), rdx);
            __ subq(ff_position(), Immediate(kCharSize));
            __ movq(rsi, backward_match());
            __ PushAllRegisters();
            __ CallCpp(FUNCTION_ADDR(RegisterMatch));
            __ PopAllRegisters();
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
      __ movq(ff_found_state(), Immediate(-1));

  } else {
    if (direction == kForward) {
      __ Move(rax, 0);
      __ cmpq(forward_match(), Immediate(0));
      __ j(zero, &exit);

      // We have a match!
      __ Move(rax, 1);

      if (match_type == kMatchFirst) {
        Register match = scratch3;
        __ movq(match, result_matches());
        __ cmpq(match, Immediate(0));
        __ j(zero, &exit);

        __ movq(scratch1, backward_match());
        __ movq(scratch2, forward_match());
        __ movq(Operand(match, offsetof(Match, begin)), scratch1);
        __ movq(Operand(match, offsetof(Match, end)),   scratch2);

      } else {  // kMatchAll
        Label keep_searching;
        Register match = rdi;

        // Register the match.
        // rsi: exit start.
        __ movq(match, result_matches());
        __ cmpq(match, Immediate(0));
        __ j(zero, &keep_searching);
        // TODO(rames): should the string pointer be rdx?
        __ movq(rdx, forward_match());
        __ movq(ff_position(), rdx);
        __ subq(ff_position(), Immediate(kCharSize));
        __ movq(last_match_end(), rdx);
        __ movq(rsi, backward_match());
        __ PushAllRegisters();
        __ CallCpp(FUNCTION_ADDR(RegisterMatch));
        __ PopAllRegisters();
        ClearAllTimes();
        __ jmp(fast_forward);

        __ bind(&keep_searching);
      }


    } else {
      Label match;
      ClearAllTimes();
      // Check if we found a match but were hoping to find a longer one.
      __ cmpq(backward_match(), Immediate(0));
      __ j(not_zero, &match);
      __ movq(ff_found_state(), Immediate(-1));
      __ jmp(&exit);

      __ bind(&match);
      __ movb(rax, Immediate(1));
      __ movq(string_pointer, backward_match());
      __ movq(scratch2, ff_found_state());
      SetStateForce(0, scratch2);
      __ movq(string_pointer, ff_position());
    }
  }

  __ bind(&exit);

}


void NB_Codegen::VisitAlternation(Alternation* regexp) {
  UNREACHABLE();
}


void NB_Codegen::VisitConcatenation(Concatenation* regexp) {
  UNREACHABLE();
}


void NB_Codegen::VisitEpsilon(Epsilon* epsilon) {
  DirectionSetOutputFromEntry(0, epsilon);
}


void NB_Codegen::VisitStartOfLine(StartOfLine* sol) {
  Label skip, match;
  DirectionTestEntryState(0, sol);
  __ j(zero, &skip);

  __ cmpq(string_pointer, string_base);
  __ j(equal, &match);

  __ cmpb(previous_char, Immediate('\n'));
  __ setcc(equal, rax);
  __ cmpb(previous_char, Immediate('\r'));
  __ setcc(equal, scratch);

  __ or_(rax, scratch);
  __ cmpb_al(Immediate(1));
  __ j(not_equal, &skip);

  __ bind(&match);
  DirectionSetOutputFromEntry(0, sol);
  __ bind(&skip);
}


void NB_Codegen::VisitEndOfLine(EndOfLine* eol) {
  Label skip, match;
  DirectionTestEntryState(0, eol);
  __ j(zero, &skip);

  __ cmpb(current_char, Immediate('\n'));
  __ setcc(equal, rax);
  __ cmpb(current_char, Immediate('\r'));
  __ setcc(equal, scratch);
  __ or_(rax, scratch);
  __ cmpb(current_char, Immediate('\0'));
  __ setcc(equal, scratch);
  __ or_(rax, scratch);

  __ cmpb_al(Immediate(1));
  __ j(not_equal, &skip);

  __ bind(&match);
  DirectionSetOutputFromEntry(0, eol);
  __ bind(&skip);
}


void NB_Codegen::VisitMultipleChar(MultipleChar* mc) {
  Label no_match;
  unsigned n_chars = mc->chars_length();

  // If matching backward, there is no terminating character to help the match
  // fail.
  if (direction() == kBackward) {
    __ subq(string_pointer, Immediate(kCharSize));
    __ movq(scratch, string_pointer);
    __ subq(scratch, Immediate(mc->chars_length()));
    __ cmpq(scratch, string_base);
    __ j(below, &no_match);
  }

  __ movq(rsi, string_pointer);
  // TODO(rames): Clean that!
  // TODO(rames): This assumes that the regexp is still accessible at
  // runtime. (mc->chars() points to the regexp string)
  if (direction() == kForward) {
    __ Move(rdi, (uint64_t)(mc->chars()));
  } else {
    __ Move(rdi, (uint64_t)(mc->chars() + mc->chars_length() - 1));
  }
  // TODO(rames): Work out the best options depending on the maximum number of
  // characters to match.
  if (n_chars >= 8) {
    __ movq(rcx, Immediate(n_chars / 8));
    __ repnecmpsq();
    // TODO(rames): the jump is probably not necessary as the next instructions
    // won't apply if the flags are not equal.
    __ j(not_equal, &no_match);
  }

  if (n_chars % 8 > 0) {
    __ movq(rcx, Immediate(n_chars % 8));
    __ repnecmpsb();
    __ j(not_equal, &no_match);
  }

  DirectionSetOutputFromEntry(mc->chars_length(), mc);
  __ bind(&no_match);
  if (direction() == kBackward) {
    __ addq(string_pointer, Immediate(kCharSize));
  }
}


void NB_Codegen::VisitPeriod(Period* period) {
  // Match all characters exept '\n' and '\r'.
  Label no_match;

  if (direction() == kBackward) {
    __ subq(string_pointer, Immediate(kCharSize));
    // TODO(rames): need this for others.
    __ cmpq(string_pointer, string_base);
    __ j(below, &no_match);
  }

  __ cmpb(current_char, Immediate('\n'));
  __ setcc(equal, rax);
  __ cmpb(current_char, Immediate('\r'));
  __ setcc(equal, scratch);
  __ or_(rax, scratch);
  __ cmpb_al(Immediate(1));
  __ j(equal, &no_match);
  DirectionSetOutputFromEntry(1, period);

  __ bind(&no_match);
  if (direction() == kBackward) {
    __ addq(string_pointer, Immediate(kCharSize));
  }
}


void NB_Codegen::VisitBracket(Bracket* bracket) {
  Label out;

  if (direction() == kBackward) {
    __ subq(string_pointer, Immediate(kCharSize));
    // TODO(rames): need this for others.
    __ cmpq(string_pointer, string_base);
    __ j(below, &out);
  }

  __ Move(scratch1, 0);
  __ Move(scratch2, 0);
  __ Move(scratch3, 0);

  // Set scratch3 if there is a match.
  vector<char>::const_iterator it;
  for (it = bracket->single_chars()->begin();
       it < bracket->single_chars()->end();
       it++) {
    __ cmpb(current_char, Immediate(*it));
    __ setcc(equal, scratch1);
    __ or_(scratch3, scratch1);
  }

  vector<Bracket::CharRange>::const_iterator rit;
  for (rit = bracket->char_ranges()->begin();
       rit < bracket->char_ranges()->end();
       rit++) {
    __ cmpb(current_char, Immediate((*rit).low));
    __ setcc(greater_equal, scratch1);
    __ cmpb(current_char, Immediate((*rit).high));
    __ setcc(less_equal, scratch2);
    __ and_(scratch1, scratch2);
    __ or_(scratch3, scratch1);
  }

  __ testb(scratch3, Immediate(1));
  if (!(bracket->flags() & Bracket::non_matching)) {
    __ j(zero, &out);
  } else {
    __ j(not_zero, &out);
  }
  DirectionSetOutputFromEntry(1, bracket);
  __ bind(&out);
  if (direction() == kBackward) {
    __ addq(string_pointer, Immediate(kCharSize));
  }
}


void NB_Codegen::VisitRepetition(Repetition* repetition) {
  UNREACHABLE();
}


void NB_Codegen::Advance(unsigned n_chars) {
  if (direction() == kForward) {
    __ addq(string_pointer, Immediate(kCharSize));
  } else {
    __ subq(string_pointer, Immediate(kCharSize));
  }
}


// TODO(rames): optimize when state_ring_size is a power of 2.
void NB_Codegen::TestState(int time, int state_index) {
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
void NB_Codegen::SetState(int target_time,
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


void NB_Codegen::SetStateForce(int target_time, int target_index) {
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


void NB_Codegen::SetStateForce(int target_time, Register target_index) {
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


void NB_Codegen::DirectionTestEntryState(int time, Regexp* regexp) {
  TestState(0, direction_ == kForward ? regexp->entry_state()
                                      : regexp->output_state());
}


void NB_Codegen::DirectionSetOutputFromEntry(int time, Regexp* regexp) {
  if (direction() == kForward) {
    SetState(time, regexp->output_state(), regexp->entry_state());
  } else {
    SetState(time, regexp->entry_state(), regexp->output_state());
  }
}


void NB_Codegen::ClearTime(int time) {
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


void NB_Codegen::ClearAllTimes() {
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


void NB_Codegen::set_direction(Direction dir) {
  direction_ = dir;
  if (direction() == kForward) {
    __ cld();
  } else {
    __ std();
  }
}


// NB_FastForwardGen -----------------------------------------------------------

void NB_FastForwardGen::Generate() {
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
    if (CpuFeatures::IsSupported(SSE4_2) &&
        multiple_chars_only &&
        regexp_list_->size() < XMMRegister::kNumRegisters - 1) {
      /* Pre-load the XMM registers for MultipleChars. */
      static const int first_xmm_fixed_code = 4;
      for (unsigned i = 0; i < regexp_list_->size(); i++) {
        __ movdqp(XMMRegister::from_code(first_xmm_fixed_code + i),
                  regexp_list_->at(i)->AsMultipleChar()->chars(),
                  regexp_list_->at(i)->AsMultipleChar()->chars_length());
      }

      Label align, align_loop;
      Label simd_code, simd_loop;
      Label maybe_match, keep_searching, find_null, exit;

      __ bind(&align);
      // Align the string pointer on a 16 bytes boundary before entering the
      // SIMD path.
      // This allows to avoid potentially illegal accesses after the eos.
      __ subq(string_pointer, Immediate(kCharSize));
      __ bind(&align_loop);
      __ addq(string_pointer, Immediate(kCharSize));
      // Check for eos character. We may as well do this before the alignment
      // check to maybe skip a useless pass.
      __ cmpb(Operand(string_pointer, 0), Immediate(0));
      __ j(zero, &exit);
      // Check the alignment.
      __ testq(string_pointer, Immediate(0xf));
      __ j(zero, &simd_code);
      // Check for matches.
      // TODO: Could we safely wider cmpsX instructions?
      for (unsigned i = 0; i < regexp_list_->size(); i++) {
        Label no_match;
        MultipleChar *mc = regexp_list_->at(i)->AsMultipleChar();
        __ movq(rsi, string_pointer);
        __ Move(rdi, (uint64_t)(mc->chars()));
        __ Move(rcx, mc->chars_length());
        __ repnecmpsb();
        __ j(not_equal, &no_match);
        // TODO: If the code generated by FoundState is non negligeable, use
        // labels to merge with the FoundState code generated below.
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

      __ bind(&simd_loop);
      // Look for potential matches.
      // Check if the eos is found, and keep track of the earlier match.
      // TODO: Instead of tracking the lowest index, track a mask of potential
      // matches and process them all. This may increase the speed for higher
      // densities of potential matches.
      Register low_index = scratch2;
      Register null_found = scratch3;
      __ Move(low_index, 0x10);
      __ Move(null_found, 0);
      __ movdqa(xmm0, Operand(string_pointer, 0));

      for (unsigned i = 0; i < regexp_list_->size(); i++) {
        __ pcmpistri(pcmp_str_control,
                     XMMRegister::from_code(first_xmm_fixed_code + i), xmm0);
        __ setcc(zero, scratch);
        __ or_(null_found, scratch);
        __ cmpq(rcx, low_index);
        __ cmovq(below, low_index, rcx);
      }

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
        // TODO: We could use pre loaded registers if there are enough. Or maybe
        // move from xmm registers if it is faster.
        // TODO: For mcs with many characters, a tighter check is probably a
        // good idea.
        Label no_match;
        MultipleChar *mc = regexp_list_->at(i)->AsMultipleChar();
        __ MoveCharsFrom(scratch, mc->chars_length(), mc->chars());
        __ cmp(mc->chars_length(), scratch, Operand(string_pointer, 0));
        __ j(not_equal, &no_match);
        FoundState(0, mc->entry_state());
        __ jmp(&exit);
        __ bind(&no_match);
      }
      __ jmp(&align);

      __ bind(&exit);

    } else {
      Label loop, potential_match;
      potential_match_ = &potential_match;

      __ bind(&loop);

      __ movb(rax, current_char);
      __ cmpb_al(Immediate(0));
      __ j(zero, &potential_match);

      for (it = regexp_list_->begin(); it < regexp_list_->end(); it++) {
        Visit(*it);
      }

      __ addq(string_pointer, Immediate(kCharSize));

      __ jmp(&loop);

      __ bind(&potential_match);
    }
  }
}


void NB_FastForwardGen::FoundState(int time, int state) {
  __ movq(ff_found_state(), Immediate(state));
  if (state >= 0) {
    codegen_->SetStateForce(time, state);
  }
}


// Single visitors ---------------------------------------------------

void NB_FastForwardGen::VisitSingleMultipleChar(MultipleChar* mc) {
  int n_chars = mc->chars_length();

  if (CpuFeatures::IsSupported(SSE4_2)) {
    Label align, align_loop;
    Label simd_code, simd_loop;
    Label potential_match, found, exit;

    Register fixed_chars = scratch3;
    __ MoveCharsFrom(fixed_chars, n_chars, mc->chars());

    __ bind(&align);
    __ subq(string_pointer, Immediate(kCharSize));

    __ bind(&align_loop);
    __ addq(string_pointer, Immediate(kCharSize));
    __ cmpb(Operand(string_pointer, 0), Immediate(0));
    __ j(zero, &exit);
    __ testq(string_pointer, Immediate((1 << 4) - 1));
    __ j(zero, &simd_code);

    __ movq(rsi, string_pointer);
    __ Move(rdi, (uint64_t)(mc->chars()));
    __ Move(rcx, n_chars);
    __ repnecmpsb();
    __ j(equal, &found);

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

    __ cmp(n_chars, fixed_chars, Operand(string_pointer, 0));
    __ j(not_equal, &align);

    __ bind(&found);
    FoundState(0, mc->entry_state());
    __ bind(&exit);

  } else {
    Label loop, done, exit;
    Register fixed_chars = scratch3;

    __ MoveCharsFrom(fixed_chars, n_chars, mc->chars());
    __ subq(string_pointer, Immediate(kCharSize));

    // TODO(rames): Is it more efficient to increment an offset rather than the
    // register?
    __ bind(&loop);
    __ addq(string_pointer, Immediate(kCharSize));
    __ mov(n_chars, rax, Operand(string_pointer, 0));
    __ cmpb_al(Immediate(0));
    __ j(equal, &exit);
    __ cmp(n_chars, rax, fixed_chars);
    __ j(not_equal, &loop);

    __ bind(&done);
    FoundState(0, mc->entry_state());
    __ bind(&exit);
  }
}


// TODO(rames): slow single visitors are all too similar not to be refactord!
void NB_FastForwardGen::VisitSinglePeriod(Period* period) {
  // TODO(rames): we probably never want to have a single ff for a period!!
  // Need to refactor the ff finder mechanisms.
  Label loop, done;

  __ subq(string_pointer, Immediate(1 * kCharSize));

  __ bind(&loop);
  __ addq(string_pointer, Immediate(1 * kCharSize));

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


void NB_FastForwardGen::VisitSingleBracket(Bracket* bracket) {
  // TODO(rames): Allow more complex brackets.
//  if (CpuFeatures::IsSupported(SSE4_2) &&
//      bracket->single_chars()->size() < 16 &&
//      bracket->char_ranges()->size() < 8) {
//
//    //__ stop("sse");
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
//    __ subq(string_pointer, Immediate(kCharSize));
//    __ bind(&find_null);
//    __ addq(string_pointer, Immediate(kCharSize));
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
//    //__ subq(string_pointer, Immediate(kCharSize));
//    //__ bind(&find_null);
//    //__ addq(string_pointer, Immediate(kCharSize));
//    //__ movb(rax, Operand(string_pointer, 0));
//    //__ cmpb_al(Immediate(0));
//    //__ j(not_zero, &find_null);
//    //__ jmp(&exit);
//
//  } else {
    Label loop, match, done;
    Label* target =
      bracket->flags() & Bracket::non_matching ? &done : &match;

    __ subq(string_pointer, Immediate(1 * kCharSize));

    __ bind(&loop);
    __ addq(string_pointer, Immediate(1 * kCharSize));

    __ movb(rax, current_char);

    __ cmpb_al(Immediate(0));
    __ j(equal, &done);

    __ Move(scratch1, 0);
    __ Move(scratch2, 0);

    vector<char>::const_iterator it;
    for (it = bracket->single_chars()->begin();
         it < bracket->single_chars()->end();
         it++) {
      __ cmpb_al(Immediate(*it));
      __ j(equal, target);
    }

    vector<Bracket::CharRange>::const_iterator rit;
    for (rit = bracket->char_ranges()->begin();
         rit < bracket->char_ranges()->end();
         rit++) {
      __ cmpb_al(Immediate((*rit).low));
      __ setcc(greater_equal, scratch1);
      __ cmpb_al(Immediate((*rit).high));
      __ setcc(less_equal, scratch2);
      __ and_(scratch1, scratch2);
      __ j(not_zero, target);
    }

    __ jmp(&loop);

    __ bind(&match);
    FoundState(0, bracket->entry_state());
    __ bind(&done);
  //}

}


// TODO: Merge VisitSingleStartOfLine and VisitSingleEndOfLine code.
void NB_FastForwardGen::VisitSingleStartOfLine(StartOfLine* sol) {
  if (CpuFeatures::IsSupported(SSE4_2)) {
    Label align, align_loop;
    Label simd_code, simd_loop;
    Label adjust, simd_adjust, found, exit;

    __ cmpq(string_pointer, string_base);
    __ j(equal, &found);

    __ bind(&align);
    __ subq(string_pointer, Immediate(kCharSize));
    __ bind(&align_loop);
    __ addq(string_pointer, Immediate(kCharSize));
    __ cmpb(Operand(string_pointer, 0), Immediate(0));
    __ j(zero, &exit);
    __ testq(string_pointer, Immediate((1 << 4) - 1));
    __ j(zero, &simd_code);

    __ cmpb(current_char, Immediate('\n'));
    __ j(equal, &adjust);
    __ cmpb(current_char, Immediate('\r'));
    __ j(equal, &adjust);
    __ jmp(&align_loop);


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
    __ jmp(&exit);

    // Adjust the string pointer, which should point to the character after the new line.
    __ bind(&simd_adjust);
    __ addq(string_pointer, rcx);
    __ bind(&adjust);
    __ addq(string_pointer, Immediate(kCharSize));

    __ bind(&found);
    FoundState(0, sol->entry_state());
    __ bind(&exit);

  } else {
    Label loop, match, done;

    // Check if we are at the beginning of the string.
    __ cmpq(string_pointer, string_base);
    __ j(equal, &match);

    __ subq(string_pointer, Immediate(1 * kCharSize));

    __ bind(&loop);
    __ addq(string_pointer, Immediate(1 * kCharSize));

    __ cmpb(current_char, Immediate(0));
    __ j(equal, &done);

    __ movb(rax, previous_char);
    __ cmpb_al(Immediate('\n'));
    __ j(equal, &match);
    __ cmpb_al(Immediate('\r'));
    __ j(equal, &match);

    __ jmp(&loop);

    __ bind(&match);
    FoundState(0, sol->entry_state());
    __ bind(&done);
  }
}


void NB_FastForwardGen::VisitSingleEndOfLine(EndOfLine* eol) {
  if (CpuFeatures::IsSupported(SSE4_2)) {
    Label align, align_loop;
    Label simd_code, simd_loop;
    Label adjust, simd_adjust, found, exit;

    __ bind(&align);
    __ subq(string_pointer, Immediate(kCharSize));
    __ bind(&align_loop);
    __ addq(string_pointer, Immediate(kCharSize));
    __ cmpb(Operand(string_pointer, 0), Immediate(0));
    __ j(zero, &found);
    __ testq(string_pointer, Immediate((1 << 4) - 1));
    __ j(zero, &simd_code);

    __ cmpb(current_char, Immediate('\n'));
    __ j(equal, &found);
    __ cmpb(current_char, Immediate('\r'));
    __ j(equal, &found);
    __ jmp(&align_loop);


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
    __ j(not_equal, &adjust);

    __ AdvanceToEOS();
    __ jmp(&found);

    __ bind(&adjust);
    __ addq(string_pointer, rcx);

    __ bind(&found);
    FoundState(0, eol->entry_state());
    __ bind(&exit);

  } else {
    Label loop, match, done;

    __ subq(string_pointer, Immediate(1 * kCharSize));

    __ bind(&loop);
    __ addq(string_pointer, Immediate(1 * kCharSize));

    __ movb(rax, current_char);

    __ cmpb_al(Immediate(0));
    __ j(equal, &match);
    __ cmpb_al(Immediate('\n'));
    __ j(equal, &match);
    __ cmpb_al(Immediate('\r'));
    __ j(equal, &match);

    __ jmp(&loop);

    __ bind(&match);
    FoundState(0, eol->entry_state());
    __ bind(&done);
  }
}


void NB_FastForwardGen::VisitSingleEpsilon(Epsilon* epsilon) {
  UNREACHABLE();
}


// Generic visitors --------------------------------------------------

// TODO(rames): Also benchmark cases with a high density of potential matches
// and see if we should try to check more characters.
void NB_FastForwardGen::VisitMultipleChar(MultipleChar* mc) {
  Label no_match;
  __ cmp(mc->chars_length(), current_chars, mc->first_chars());
  __ j(not_equal, &no_match);
  FoundState(0, mc->entry_state());
  __ jmp(potential_match_);
  __ bind(&no_match);
}


void NB_FastForwardGen::VisitPeriod(Period*) {
  UNIMPLEMENTED();
}


void NB_FastForwardGen::VisitBracket(Bracket* bracket) {
  UNIMPLEMENTED();
}


void NB_FastForwardGen::VisitStartOfLine(StartOfLine*) {
  UNIMPLEMENTED();
}


void NB_FastForwardGen::VisitEndOfLine(EndOfLine*) {
  UNIMPLEMENTED();
}


void NB_FastForwardGen::VisitEpsilon(Epsilon* epsilon) {
  UNREACHABLE();
}


// NB_FastForwardGen_iter -----------------------------------------------------------

void NB_FastForwardGen_iter::VisitEntryState(int entry_state) {
  vector<Regexp*>::iterator it, it_s;

  Regexp* only_re_at_entry = single_re_at_entry(&regexp_list_, entry_state);

  if (only_re_at_entry) {
    // If we only have one entry regexp try to use fast specialized code.
    VisitSingle(only_re_at_entry);

  } else {
    // We have multiple possible entry regexps.
    // Loop and exit when a potential match is found.
    Label loop, potential_match;
    potential_match_ = &potential_match;

    __ bind(&loop);
    __ cmpb(current_char, Immediate(0));
    __ j(equal, &potential_match);

    VisitState(entry_state);

    __ addq(string_pointer, Immediate(kCharSize));
    __ jmp(&loop);

    __ bind(&potential_match);
  }
}

void NB_FastForwardGen_iter::VisitState(int state) {
  vector<Regexp*>::iterator it;
  for (it = regexp_list_.begin();
       it < regexp_list_.end() && (*it)->entry_state() < state;
       it++) {}
  for (;
       it < regexp_list_.end() && (*it)->entry_state() == state;
       it++) {
      Visit(*it);
  }
}


// Single visitors ---------------------------------------------------

// TODO(rames): If we knew the length of the string we could use scasb.
// TODO(rames): SIMD could be useful here.
void NB_FastForwardGen_iter::VisitSingleMultipleChar(MultipleChar* mc) {
  Label done, loop;
  Register fixed_chars = scratch3;

  int n_chars = mc->chars_length();

  if (ff_pre_scan()) {
    __ movb(rax, Immediate(mc->chars()[0]));
    __ MoveCharsFrom(fixed_chars, n_chars - 1, mc->chars() + 1);
    __ movq(rdi, string_pointer);
    __ movq(rcx, end_of_string());
    __ subq(rcx, rdi);

    __ bind(&loop);
    __ repnescasb();

    __ j(not_equal, &done);

    __ mov(n_chars - 1, scratch1, Operand(rdi, 0));
    __ cmp(n_chars - 1, scratch1, fixed_chars);
    __ j(not_equal, &loop);

    __ decq(rdi);
    __ bind(&done);
    __ movq(string_pointer, rdi);

  } else {
    __ MoveCharsFrom(fixed_chars, n_chars, mc->chars());
    __ subq(string_pointer, Immediate(kCharSize));

    // TODO(rames): Is it more efficient to increment an offset rather than the
    // register?
    __ bind(&loop);
    __ addq(string_pointer, Immediate(kCharSize));
    __ mov(n_chars, rax, Operand(string_pointer, 0));
    __ cmpb_al(Immediate(0));
    __ j(equal, &done);
    __ cmp(n_chars, rax, fixed_chars);
    __ j(not_equal, &loop);
    __ bind(&done);
  }

}


// TODO(rames): slow single visitors are all too similar not to be refactord!
void NB_FastForwardGen_iter::VisitSinglePeriod(Period* period) {
  Label done;
  Regexp* single_next = single_re_at_entry(&regexp_list_, period->output_state());

  if (single_next) {
    // TODO(rames): Introduce a different label for the end of the string to
    // avoid nesting end of string tests..
    Label ff_next_regexp;

    __ bind(&ff_next_regexp);
    // Start matching from the next character.
    // Make sure that we are not already at the end of the string.
    __ cmpb(current_char, Immediate(0));
    __ j(equal, &done);
    __ addq(string_pointer, Immediate(kCharSize));

    VisitSingle(single_next);

    __ subq(string_pointer, Immediate(kCharSize));
    __ movb(rax, current_char);
    // We have a potential match for the following regexp or we reached the end
    // of the string.
    __ cmpb_al(Immediate(0));
    __ j(equal, &done);

    // Check if there is also a match for the period.
    __ cmpb_al(Immediate('\n'));
    __ setcc(equal, scratch1);
    __ cmpb_al(Immediate('\r'));
    __ setcc(equal, scratch2);
    __ or_(scratch1, scratch2);
    __ cmpb(scratch1, Immediate(1));
    __ j(not_equal, &done);

    __ addq(string_pointer, Immediate(2 * kCharSize));
    __ jmp(&ff_next_regexp);

  } else {
    Label loop;

    __ Move(scratch1, 0);
    __ Move(scratch2, 0);
    __ Move(scratch3, 0);

    __ bind(&loop);
    __ movb(rax, current_char);
    __ cmpb_al(Immediate(0));
    __ j(equal, &done);

    __ cmpb_al(Immediate('\n'));
    __ setcc(equal, scratch1);
    __ cmpb_al(Immediate('\r'));
    __ setcc(equal, scratch2);
    __ or_(scratch1, scratch2);
    __ cmpb(scratch1, Immediate(1));
    __ j(not_equal, &done);

    __ addq(string_pointer, Immediate(kCharSize));
    __ jmp(&loop);

  }

  __ bind(&done);
}


void NB_FastForwardGen_iter::VisitSingleBracket(Bracket* bracket) {
  Label done;
  Regexp* single_next = single_re_at_entry(&regexp_list_, bracket->output_state());

  if (single_next) {
    // TODO(rames): Introduce a different label for the end of the string to
    // avoid nesting end of string tests..
    Label ff_next_regexp;

    __ bind(&ff_next_regexp);
    // Start matching from the next character.
    // Make sure that we are not already at the end of the string.
    __ cmpb(current_char, Immediate(0));
    __ j(equal, &done);
    __ addq(string_pointer, Immediate(kCharSize));

    VisitSingle(single_next);

    // We have a potential match for the following regexp or we reached the end
    // of the string.
    __ cmpb(previous_char, Immediate(0));
    __ j(equal, &done);

    // Check if there is also a match for the bracket.
    Label advance;
    Label* target = bracket->flags() & Bracket::non_matching ? &advance : &done;
    __ subq(string_pointer, Immediate(kCharSize));
    __ Move(scratch1, 0);
    __ Move(scratch2, 0);
    __ Move(scratch3, 0);

    vector<char>::const_iterator it;
    for (it = bracket->single_chars()->begin();
         it < bracket->single_chars()->end();
         it++) {
      __ cmpb(current_char, Immediate(*it));
      __ j(equal, target);
    }

    vector<Bracket::CharRange>::const_iterator rit;
    for (rit = bracket->char_ranges()->begin();
         rit < bracket->char_ranges()->end();
         rit++) {
      __ cmpb(current_char, Immediate((*rit).low));
      __ setcc(greater_equal, scratch1);
      __ cmpb(current_char, Immediate((*rit).high));
      __ setcc(less_equal, scratch2);
      __ and_(scratch1, scratch2);
      __ j(not_zero, target);
    }

    // Advance 2 characters as we decremented by one above for the bracket checks.
    __ bind(&advance);
    __ addq(string_pointer, Immediate(2 * kCharSize));
    __ jmp(&ff_next_regexp);

  } else {
    Label loop;

    __ Move(scratch1, 0);
    __ Move(scratch2, 0);
    __ Move(scratch3, 0);

    __ bind(&loop);
    __ movb(rax, current_char);
    __ cmpb_al(Immediate(0));
    __ j(zero, &done);

    vector<char>::const_iterator it;
    for (it = bracket->single_chars()->begin();
         it < bracket->single_chars()->end();
         it++) {
      __ cmpb_al(Immediate(*it));
      __ j(equal, &done);
    }

    vector<Bracket::CharRange>::const_iterator rit;
    for (rit = bracket->char_ranges()->begin();
         rit < bracket->char_ranges()->end();
         rit++) {
      __ cmpb_al(Immediate((*rit).low));
      __ setcc(greater_equal, scratch1);
      __ cmpb_al(Immediate((*rit).high));
      __ setcc(less_equal, scratch2);
      __ and_(scratch1, scratch2);
      __ j(not_zero, &done);
    }

    __ addq(string_pointer, Immediate(kCharSize));
    __ jmp(&loop);

  }

  __ bind(&done);
}


void NB_FastForwardGen_iter::VisitSingleStartOfLine(StartOfLine* sol) {
  Label done;
  Regexp* single_next = single_re_at_entry(&regexp_list_, sol->output_state());

  if (single_next) {
    // TODO(rames): Introduce a different label for the end of the string to
    // avoid nesting end of string tests..
    Label ff_next_regexp;

    __ bind(&ff_next_regexp);
    // Make sure that we are not already at the end of the string.
    __ cmpb(current_char, Immediate(0));
    __ j(equal, &done);

    VisitSingle(single_next);

    __ cmpq(string_pointer, string_base);
    __ j(equal, &done);

    __ cmpb(current_char, Immediate(0));
    __ j(equal, &done);

    __ movb(rax, previous_char);

    // Check if there is also a match for the sol.
    __ cmpb_al(Immediate('\n'));
    __ setcc(equal, scratch1);
    __ cmpb_al(Immediate('\r'));
    __ setcc(equal, scratch2);
    __ or_(scratch1, scratch2);
    __ cmpb(scratch1, Immediate(1));
    __ j(equal, &done);

    __ addq(string_pointer, Immediate(kCharSize));
    __ jmp(&ff_next_regexp);

  } else {
    Label loop;

    __ cmpq(string_pointer, string_base);
    __ j(equal, &done);

    __ Move(scratch1, 0);
    __ Move(scratch2, 0);

    __ bind(&loop);
    __ movb(rax, current_char);
    __ cmpb_al(Immediate(0));
    __ j(equal, &done);

    __ movb(rax, previous_char);
    __ cmpb_al(Immediate('\n'));
    __ setcc(equal, scratch1);
    __ cmpb_al(Immediate('\r'));
    __ setcc(equal, scratch2);
    __ or_(scratch1, scratch2);
    __ cmpb(scratch1, Immediate(1));
    __ j(equal, &done);

    __ addq(string_pointer, Immediate(kCharSize));
    __ jmp(&loop);

  }

  __ bind(&done);
}


void NB_FastForwardGen_iter::VisitSingleEndOfLine(EndOfLine* eol) {
  Label done;
  Regexp* single_next = single_re_at_entry(&regexp_list_, eol->output_state());

  if (single_next) {
    // TODO(rames): Introduce a different label for the end of the string to
    // avoid nesting end of string tests..
    Label ff_next_regexp;

    __ bind(&ff_next_regexp);
    // Make sure that we are not already at the end of the string.
    __ cmpb(current_char, Immediate(0));
    __ j(equal, &done);

    VisitSingle(single_next);

    __ movb(rax, current_char);

    __ cmpb_al(Immediate(0));
    __ j(equal, &done);

    // Check if there is also a match for the eol.
    __ cmpb_al(Immediate('\n'));
    __ setcc(equal, scratch1);
    __ cmpb_al(Immediate('\r'));
    __ setcc(equal, scratch2);
    __ or_(scratch1, scratch2);
    __ cmpb(scratch1, Immediate(1));
    __ j(equal, &done);

    __ addq(string_pointer, Immediate(kCharSize));
    __ jmp(&ff_next_regexp);

  } else {
    Label loop;

    __ Move(scratch1, 0);
    __ Move(scratch2, 0);

    __ bind(&loop);
    __ movb(rax, current_char);
    __ cmpb_al(Immediate(0));
    __ j(equal, &done);

    __ cmpb_al(Immediate('\n'));
    __ setcc(equal, scratch1);
    __ cmpb_al(Immediate('\r'));
    __ setcc(equal, scratch2);
    __ or_(scratch1, scratch2);
    __ cmpb(scratch1, Immediate(1));
    __ j(equal, &done);

    __ addq(string_pointer, Immediate(kCharSize));
    __ jmp(&loop);

  }

  __ bind(&done);
}


void NB_FastForwardGen_iter::VisitSingleEpsilon(Epsilon* epsilon) {
  VisitEntryState(epsilon->output_state());
}


// Generic visitors --------------------------------------------------

// TODO(rames): Also benchmark cases with a high density of potential matches
// and see if we should try to check more characters.
void NB_FastForwardGen_iter::VisitMultipleChar(MultipleChar* mc) {
  __ cmp(mc->chars_length(), current_chars, mc->first_chars());
  __ j(equal, potential_match_);
}


void NB_FastForwardGen_iter::VisitPeriod(Period*) {
  __ cmpb(current_char, Immediate('\n'));
  __ setcc(equal, rax);
  __ cmpb(current_char, Immediate('\r'));
  __ setcc(equal, scratch);
  __ or_(rax, scratch);
  __ cmpb_al(Immediate(1));
  __ j(not_zero, potential_match_);
}


void NB_FastForwardGen_iter::VisitBracket(Bracket* bracket) {
  __ Move(scratch1, 0);
  __ Move(scratch2, 0);
  __ Move(scratch3, 0);

  __ movb(rax, current_char);
  // Set scratch3 if there is a match.
  vector<char>::const_iterator it;
  for (it = bracket->single_chars()->begin();
       it < bracket->single_chars()->end();
       it++) {
    __ cmpb_al(Immediate(*it));
    __ setcc(equal, scratch1);
    __ or_(scratch3, scratch1);
  }

  vector<Bracket::CharRange>::const_iterator rit;
  for (rit = bracket->char_ranges()->begin();
       rit < bracket->char_ranges()->end();
       rit++) {
    __ cmpb_al(Immediate((*rit).low));
    __ setcc(greater_equal, scratch1);
    __ cmpb_al(Immediate((*rit).high));
    __ setcc(less_equal, scratch2);
    __ and_(scratch1, scratch2);
    __ or_(scratch3, scratch1);
  }

  __ testb(scratch3, Immediate(1));
  if (!(bracket->flags() & Bracket::non_matching)) {
    __ j(zero, potential_match_);
  } else {
    __ j(not_zero, potential_match_);
  }
}


void NB_FastForwardGen_iter::VisitStartOfLine(StartOfLine*) {
  __ cmpq(string_pointer, string_base);
  __ j(equal, potential_match_);

  __ cmpb(previous_char, Immediate('\n'));
  __ setcc(equal, rax);
  __ cmpb(previous_char, Immediate('\r'));
  __ setcc(equal, scratch);

  __ or_(rax, scratch);
  __ cmpb_al(Immediate(1));
  __ j(equal, potential_match_);
}


void NB_FastForwardGen_iter::VisitEndOfLine(EndOfLine*) {
  __ cmpb(current_char, Immediate('\n'));
  __ setcc(equal, rax);
  __ cmpb(current_char, Immediate('\r'));
  __ setcc(equal, scratch);
  __ or_(rax, scratch);
  __ cmpb(current_char, Immediate('\0'));
  __ setcc(equal, scratch);
  __ or_(rax, scratch);

  __ cmpb_al(Immediate(1));
  __ j(equal, potential_match_);
}


void NB_FastForwardGen_iter::VisitEpsilon(Epsilon* epsilon) {
  VisitState(epsilon->output_state());
}


} }  // namespace rejit::internal
