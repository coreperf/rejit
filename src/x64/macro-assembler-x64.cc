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

#include "macro-assembler-x64.h"

namespace rejit {
namespace internal {

MacroAssembler::MacroAssembler()
  : MacroAssemblerBase(NULL, 4 * KB) { }


// The structure of this function is copied from
//  v8 x64 MacroAssembler::LoadSmiConstant().
void MacroAssembler::Move(Register dst, uint64_t value) {
  if (value == 0) {
    xorl(dst, dst);
    return;
  }
  // TODO(rames): Optimize this!
  if (value & 0xFFFFFFFF00000000ULL) {
    movq(dst, Immediate(value >> 32));
    shl(dst, Immediate(32));
    movl(mscratch, Immediate(value & 0xFFFFFFFF));
    or_(dst, mscratch);
  } else {
    movq(dst, Immediate(value & 0xFFFFFFFF));
  }
}


//      const int64_t c = *reinterpret_cast<const int64_t*>(next_chars);
//      __ Move(fixed_chars, c);
//      if (n_chars == 2) {
//        __ movb(fixed_chars, Immediate(next_chars[0]));
//      } else if (n_chars == 3) {
//        __ movw(fixed_chars, Immediate(*reinterpret_cast<const int16_t*>(next_chars)))
//      } else if (n_chars <= sizeof(uint32_t) + 1) {
//        int32_t c =
//          *reinterpret_cast<const int32_t*>(next_chars) & FirstCharsMask(n_chars);
//        __ movl(fixed_chars, Immediate(c))
//
//      } else if (n_chars <= sizeof(uint64_t)) + 1 {
//        int64_t c =
//          *reinterpret_cast<const int64_t*>(next_chars) & FirstCharsMask(n_chars);
//        __ Move(fixed_chars, Immediate(c))
//
//      } else {
//        int64_t c = *reinterpret_cast<const int64_t*>(next_chars);
//        __ Move(fixed_chars, Immediate(c))
//      }

// TODO: this does not have to zero the rest of the register.
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


void MacroAssembler::mov(unsigned width, Register dst, const Operand& src) {
  if (width == 0) return;
  if (width == 1) {
    movb(dst, src);
  } else if (width < 4) {
    movw(dst, src);
  } else if (width < 8) {
    movl(dst, src);
  } else {
    movq(dst, src);
  }
}


void MacroAssembler::cmp(unsigned width, Register r1, Register r2) {
  if (width == 0) return;
  if (width == 1) {
    cmpb(r1, r2);
  } else if (width < 4) {
    cmpw(r1, r2);
  } else if (width < 8) {
    cmpl(r1, r2);
  } else {
    cmpq(r1, r2);
  }
}


void MacroAssembler::cmp(unsigned width, Register r1, const Operand& r2) {
  if (width == 0) return;
  if (width == 1) {
    cmpb(r1, r2);
  } else if (width < 4) {
    cmpw(r1, r2);
  } else if (width < 8) {
    cmpl(r1, r2);
  } else {
    cmpq(r1, r2);
  }
}


void MacroAssembler::cmp(unsigned width, const Operand& dst, int64_t src) {
  if (width == 0) return;
  if (width == 1) {
    cmpb(dst, Immediate(src & FirstCharsMask(1)));
  } else if (width < 4) {
    cmpw(dst, Immediate(src & FirstCharsMask(2)));
  } else if (width < 8) {
    cmpl(dst, Immediate(src & FirstCharsMask(4)));
  } else {
    Move(mscratch, src);
    cmpq(dst, mscratch);
  }
}


void MacroAssembler::movdq(XMMRegister dst, uint64_t high, uint64_t low) {
  // TODO(rames): rework this.
  // It is awful but it works.
  Move(scratch, high);
  push(scratch);
  Move(scratch, low);
  push(scratch);

  movdqu(dst, Operand(rsp, 0));

  pop(scratch);
  pop(scratch);
}


void MacroAssembler::movdqp(XMMRegister dst, const char* chars, size_t n_chars) {
  // TODO(rames): rework this.
  // It is awful but it works.
    if (n_chars > 8) {
      MoveCharsFrom(scratch, n_chars - 8, chars + 8);
    } else {
      Move(scratch, 0);
    }
    push(scratch);
    MoveCharsFrom(scratch, n_chars, chars);
    push(scratch);

    movdqu(dst, Operand(rsp, 0));

    pop(scratch);
    pop(scratch);
}


void MacroAssembler::PushAllRegisters() {
  push(rax);
  push(rcx);
  push(rdx);
  push(rbx);
  push(rsi);
  push(rdi);
  push(r8);
  push(r9);
  push(r10);
  push(r11);
  push(r12);
  push(r13);
  push(r14);
  push(r15);
}


void MacroAssembler::PopAllRegisters() {
  pop(r15);
  pop(r14);
  pop(r13);
  pop(r12);
  pop(r11);
  pop(r10);
  pop(r9);
  pop(r8);
  pop(rdi);
  pop(rsi);
  pop(rbx);
  pop(rdx);
  pop(rcx);
  pop(rax);
}


void MacroAssembler::PreserveRegs() {
  push(rcx);
  push(rdx);
  push(rbx);
  push(rsi);
  push(rdi);
  push(r8);
  push(r9);
  push(r10);
  push(r11);
  push(r12);
  push(r13);
  push(r14);
  push(r15);
}


void MacroAssembler::RestoreRegs() {
  pop(r15);
  pop(r14);
  pop(r13);
  pop(r12);
  pop(r11);
  pop(r10);
  pop(r9);
  pop(r8);
  pop(rdi);
  pop(rsi);
  pop(rbx);
  pop(rdx);
  pop(rcx);
}


// TODO(rames): Push and pop ah instread of rax.
void MacroAssembler::PushAllRegistersAndFlags() {
  push(rax);
  push(rcx);
  push(rdx);
  push(rbx);
  push(rsi);
  push(rdi);
  push(r8);
  push(r9);
  push(r10);
  push(r11);
  push(r12);
  push(r13);
  push(r14);
  push(r15);
  lahf();
  push(rax);
}


void MacroAssembler::PopAllRegistersAndFlags() {
  pop(rax);
  sahf();
  pop(r15);
  pop(r14);
  pop(r13);
  pop(r12);
  pop(r11);
  pop(r10);
  pop(r9);
  pop(r8);
  pop(rdi);
  pop(rsi);
  pop(rbx);
  pop(rdx);
  pop(rcx);
  pop(rax);
}


void MacroAssembler::PrepareStack() {
  movq(scratch, rsp);
  subq(rsp, Immediate(kPointerSize));
  // TODO(rames): introduce a platform stack alignment.
  and_(rsp, Immediate(~0xf));
  movq(Operand(rsp, 0), scratch);
}


void MacroAssembler::CallCpp(Address address) {
  PrepareStack();
  Move(rax, (int64_t)address);
  call(rax);
  // Restore the stack pointer.
  movq(rsp, Operand(rsp, 0));
}


void MacroAssembler::AdvanceToEOS() {
  Label loop;
  subq(string_pointer, Immediate(kCharSize));
  bind(&loop);
  addq(string_pointer, Immediate(kCharSize));
  cmpb(current_char, Immediate(0));
  j(not_zero, &loop);
}


// Debug helpers -----------------------------------------------------

static void LocalPrint(const char* message) {
  cout << message << endl;
}


void MacroAssembler::asm_assert_(Condition cond, const char *file, int line, const char *description) {
  Label skip;
  j(cond, &skip);

  Move(rdi, (int64_t)file);
  Move(rsi, line);
  Move(rdx, (int64_t)description);
  PushAllRegistersAndFlags();
  CallCpp(FUNCTION_ADDR(rejit_fatal));
  PopAllRegistersAndFlags();

  bind(&skip);
}


void MacroAssembler::debug_msg(const char *message) {
  PushAllRegistersAndFlags();
  Move(rdi, (int64_t)message);
  CallCpp(FUNCTION_ADDR(LocalPrint));
  PopAllRegistersAndFlags();
}


void MacroAssembler::debug_msg(Condition cond, const char *message) {
  Label skip;
  if (cond != always) {
    j(NegateCondition(cond), &skip);
  }
  debug_msg(message);
  bind(&skip);
}


void MacroAssembler::stop(const char *message) {
  debug_msg(message);
  int3();
}


void MacroAssembler::stop(Condition cond, const char *message) {
  Label skip;
  if (cond != always) {
    j(NegateCondition(cond), &skip);
  }
  stop(message);
  bind(&skip);
}

} }  // namespace rejit::internal
