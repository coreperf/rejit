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

#ifndef REJIT_X64_MACRO_ASSEMBLER_X64_INL_H_
#define REJIT_X64_MACRO_ASSEMBLER_X64_INL_H_

namespace rejit {
namespace internal {


void MacroAssembler::PushRegisters(RegList regs) {
  int i;
  for (i = Register::kNumRegisters - 1; i >= 0; i--) {
    if (regs & (1 << i)) {
      push(Register::from_code(i));
    }
  }
}


void MacroAssembler::PopRegisters(RegList regs) {
  int i;
  for (i = 0; i < Register::kNumRegisters; i++) {
    if (regs & (1 << i)) {
      pop(Register::from_code(i));
    }
  }
}


// TODO: We generally don't care about all those registers and don't need to
// preserve them all.
void MacroAssembler::PushCallerSavedRegisters() {
  PushRegisters(kCallerSavedRegList);
}


void MacroAssembler::PopCallerSavedRegisters() {
  PopRegisters(kCallerSavedRegList);
}


// TODO: The saved registers are above the the frame pointer on the stack, which
// does not fit the ABI. This is because we mess up with lower the stack pointer
// while setting up the state ring (and other stuff), but don't restore it
// before returning.
void MacroAssembler::PushCalleeSavedRegisters() {
  push(rbp);
  movq(rbp, rsp);
  PushRegisters(kCalleeSavedRegList);
}


void MacroAssembler::PopCalleeSavedRegisters() {
  PopRegisters(kCalleeSavedRegList);
  pop(rbp);
}


void MacroAssembler::PushAllRegisters() {
  // All registers except rsp.
  PushRegisters(0xffff & ~rsp.bit());
}


void MacroAssembler::PopAllRegisters() {
  // All registers except rsp.
  PopRegisters(0xffff & ~rsp.bit());
}


// TODO(rames): Push and pop ah instread of rax.
void MacroAssembler::PushAllRegistersAndFlags() {
  PushAllRegisters();
  lahf();
  push(rax);
}


void MacroAssembler::PopAllRegistersAndFlags() {
  pop(rax);
  sahf();
  PopAllRegisters();
}


void MacroAssembler::debug_msg(const char *message) {
  if (FLAG_emit_debug_code) {
    msg(message);
  }
}


void MacroAssembler::Move(Register dst, Register src) {
  if (!dst.is(src)) {
    movq(dst, src);
  }
}


void MacroAssembler::MoveCharsFrom(Register dst, unsigned n, const char *location) {
  if (n == 0) return;
  int64_t chars = *reinterpret_cast<const int64_t*>(location);
  Move(dst, chars & FirstCharsMask(n));
}


void MacroAssembler::LoadCharsFrom(Register dst, unsigned n, const Operand& src) {
  ASSERT(0 < n && n <= (unsigned)kPointerSize);
  movq(dst, src);
  Move(scratch, FirstCharsMask(n));
  and_(dst, scratch);
}


void MacroAssembler::LoadCurrentChar(Register r) {
  // TODO(rames): Issue when accessing at a memory limit, eg. end of page?
  movq(r, Operand(string_pointer, 0));
}

void MacroAssembler::inc_c(Register dst) {
  if (char_size() == 1) {
    incq(dst);
  } else {
    addq(dst, Immediate(char_size()));
  }
}
void MacroAssembler::dec_c(Register dst) {
  if (char_size() == 1) {
    decq(dst);
  } else {
    subq(dst, Immediate(char_size()));
  }
}


void MacroAssembler::Advance(unsigned n_chars,
                             Direction direction,
                             Register reg) {
  if (direction == kForward) {
    if (kCharSize == 1) {
      incq(string_pointer);
    } else {
      addq(string_pointer, Immediate(kCharSize));
    }
  } else {
    if (kCharSize == 1) {
      decq(string_pointer);
    } else {
      subq(string_pointer, Immediate(kCharSize));
    }
  }
}


void MacroAssembler::AdvanceToEOS() {
  movq(string_pointer, string_end);
}


} }  // namespace rejit::internal

#endif  // REJIT_X64_MACRO_ASSEMBLER_X64_INL_H_
