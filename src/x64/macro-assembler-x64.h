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

#ifndef REJIT_X64_MACRO_ASSEMBLER_X64_H_
#define REJIT_X64_MACRO_ASSEMBLER_X64_H_

#include "assembler-x64.h"
#include "assembler-x64-inl.h"
#include "macro-assembler-base.h"

namespace rejit {
namespace internal {

const Operand string_base(rbp, -1 * kPointerSize);
const Register ring_index = r15;
const Register string_pointer = r14;
const Operand current_char = Operand(string_pointer, 0);
const Operand next_char = Operand(string_pointer, kCharSize);
const Operand previous_char = Operand(string_pointer, -kCharSize);
const Operand current_chars = Operand(string_pointer, 0);

const Register mscratch = r8;
const Register scratch = r9;
const Register scratch1 = r9;
const Register scratch2 = r10;
const Register scratch3 = r11;
const Register scratch4 = r12;


class MacroAssembler : public MacroAssemblerBase {
 public:
  MacroAssembler();
  // TODO(ajr): Check destructor need.

  void LoadCurrentChar(Register r) {
    // TODO(rames): Issue when accessing at a memory limit, eg. end of page?
    movq(r, Operand(string_pointer, 0));
  }

  void Move(Register dst, uint64_t value);

  inline void Addq(Register reg, Immediate src) {
    if (src.value_ != 0) addq(reg, src);
  }

  void MoveCharsFrom(Register dst, unsigned n, const char* chars);
  void LoadCharsFrom(Register dst, unsigned n, const Operand& src);

  void PushAllRegisters();
  void PopAllRegisters();
  void PushAllRegistersAndFlags();
  void PopAllRegistersAndFlags();

  // TODO(rames): correctly handle callee-saved.
  void PreserveRegs();
  void RestoreRegs();

  void PrepareStack();
  void CallCpp(Address address);

  // Variable width helpers.
  void mov(unsigned width, Register dst, const Operand&);

  void cmp(unsigned width, Register dst, Register src);
  void cmp(unsigned width, Register dst, const Operand& src);
  void cmp(unsigned width, const Operand& dst, int64_t src);

  void movdq(XMMRegister dst, uint64_t high, uint64_t low);
  void movdqp(XMMRegister dst, const char* chars, size_t n_chars);

  // Debug helpers ---------------------------------------------------

  // All debug helpers must preserve registers and flags.

  // Print a message.
  void debug_msg(const char* message);
  void debug_msg(Condition cond, const char* message);

  // Assert from assembly.
  // Define a macro for convenience.
  void asm_assert_(Condition cond, const char* file, int line, const char* description);

  // Print a message and stop.
  void stop(const char* messgae);
  void stop(Condition cond, const char* messgae);
#define asm_assert(cond, description) asm_assert_(cond, __FILE__, __LINE__, description)

 private:
  /* data */
};


uint64_t FirstCharsMask(int n);



} }  // namespace rejit::internal

#endif  // REJIT_X64_MACRO_ASSEMBLER_X64_H_

