// Imported from v8 git repository:
// commit 32777d04209b0ee6b21e12a8e7d7ace59bce3c23
// Date:   Thu Feb 14 15:12:49 2013 +0000
// svn id: 13673 ce2b1a6d-e550-0410-aec6-3dcde31c8c00

// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef REJIT_X64_ASSEMBLER_X64_INL_H_
#define REJIT_X64_ASSEMBLER_X64_INL_H_

#include "x64/assembler-x64.h"

#include "cpu.h"
#include "memory.h"

namespace rejit {
namespace internal {


// -----------------------------------------------------------------------------
// Implementation of Assembler


void Assembler::emitq(uint64_t x) {
  Memory::uint64_at(pc_) = x;
  pc_ += sizeof(uint64_t);
}


void Assembler::emitl(uint32_t x) {
  Memory::uint32_at(pc_) = x;
  pc_ += sizeof(uint32_t);
}


void Assembler::emitw(uint16_t x) {
  Memory::uint16_at(pc_) = x;
  pc_ += sizeof(uint16_t);
}


void Assembler::emit_rex_64(Register reg, Register rm_reg) {
  emit(0x48 | reg.high_bit() << 2 | rm_reg.high_bit());
}


void Assembler::emit_rex_64(XMMRegister reg, Register rm_reg) {
  emit(0x48 | (reg.code() & 0x8) >> 1 | rm_reg.code() >> 3);
}


void Assembler::emit_rex_64(Register reg, XMMRegister rm_reg) {
  emit(0x48 | (reg.code() & 0x8) >> 1 | rm_reg.code() >> 3);
}


void Assembler::emit_rex_64(Register reg, const Operand& op) {
  emit(0x48 | reg.high_bit() << 2 | op.rex_);
}


void Assembler::emit_rex_64(XMMRegister reg, const Operand& op) {
  emit(0x48 | (reg.code() & 0x8) >> 1 | op.rex_);
}


void Assembler::emit_rex_64(Register rm_reg) {
  ASSERT((rm_reg.code() & 0xf) == rm_reg.code());
  emit(0x48 | rm_reg.high_bit());
}


void Assembler::emit_rex_64(const Operand& op) {
  emit(0x48 | op.rex_);
}


void Assembler::emit_rex_32(Register reg, Register rm_reg) {
  emit(0x40 | reg.high_bit() << 2 | rm_reg.high_bit());
}


void Assembler::emit_rex_32(Register reg, const Operand& op) {
  emit(0x40 | reg.high_bit() << 2  | op.rex_);
}


void Assembler::emit_rex_32(Register rm_reg) {
  emit(0x40 | rm_reg.high_bit());
}


void Assembler::emit_rex_32(const Operand& op) {
  emit(0x40 | op.rex_);
}


void Assembler::emit_optional_rex_32(Register reg, Register rm_reg) {
  byte rex_bits = reg.high_bit() << 2 | rm_reg.high_bit();
  if (rex_bits != 0) emit(0x40 | rex_bits);
}


void Assembler::emit_optional_rex_32(Register reg, const Operand& op) {
  byte rex_bits =  reg.high_bit() << 2 | op.rex_;
  if (rex_bits != 0) emit(0x40 | rex_bits);
}


void Assembler::emit_optional_rex_32(XMMRegister reg, const Operand& op) {
  byte rex_bits =  (reg.code() & 0x8) >> 1 | op.rex_;
  if (rex_bits != 0) emit(0x40 | rex_bits);
}


void Assembler::emit_optional_rex_32(XMMRegister reg, XMMRegister base) {
  byte rex_bits =  (reg.code() & 0x8) >> 1 | (base.code() & 0x8) >> 3;
  if (rex_bits != 0) emit(0x40 | rex_bits);
}


void Assembler::emit_optional_rex_32(XMMRegister reg, Register base) {
  byte rex_bits =  (reg.code() & 0x8) >> 1 | (base.code() & 0x8) >> 3;
  if (rex_bits != 0) emit(0x40 | rex_bits);
}


void Assembler::emit_optional_rex_32(Register reg, XMMRegister base) {
  byte rex_bits =  (reg.code() & 0x8) >> 1 | (base.code() & 0x8) >> 3;
  if (rex_bits != 0) emit(0x40 | rex_bits);
}


void Assembler::emit_optional_rex_32(Register rm_reg) {
  if (rm_reg.high_bit()) emit(0x41);
}


void Assembler::emit_optional_rex_32(const Operand& op) {
  if (op.rex_ != 0) emit(0x40 | op.rex_);
}


Address Assembler::target_address_at(Address pc) {
  return Memory::int32_at(pc) + pc + 4;
}


void Assembler::set_target_address_at(Address pc, Address target) {
  Memory::int32_at(pc) = static_cast<int32_t>(target - pc - 4);
  CPU::FlushICache(pc, sizeof(int32_t));
}

// -----------------------------------------------------------------------------
// Implementation of Operand

void Operand::set_modrm(int mod, Register rm_reg) {
  ASSERT(is_uint2(mod));
  buf_[0] = mod << 6 | rm_reg.low_bits();
  // Set REX.B to the high bit of rm.code().
  rex_ |= rm_reg.high_bit();
}


void Operand::set_sib(ScaleFactor scale, Register index, Register base) {
  ASSERT(len_ == 1);
  ASSERT(is_uint2(scale));
  ASSERT(!uses_reloc_data());   // Note supported yet.
  // Use SIB with no index register only for base rsp or r12. Otherwise we
  // would skip the SIB byte entirely.
  ASSERT(!index.is(rsp) || base.is(rsp) || base.is(r12));
  buf_[1] = (scale << 6) | (index.low_bits() << 3) | base.low_bits();
  rex_ |= index.high_bit() << 1 | base.high_bit();
  len_ = 2;
}

void Operand::set_disp8(int disp) {
  ASSERT(is_int8(disp));
  ASSERT(len_ == 1 || len_ == 2);
  int8_t* p = reinterpret_cast<int8_t*>(&buf_[len_]);
  *p = disp;
  len_ += sizeof(int8_t);
}

void Operand::set_disp32(int disp) {
  ASSERT(len_ == 1 || len_ == 2);
  int32_t* p = reinterpret_cast<int32_t*>(&buf_[len_]);
  *p = disp;
  len_ += sizeof(int32_t);
}


} }  // namespace rejit::internal

#endif  // REJIT_X64_ASSEMBLER_X64_INL_H_
