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

#ifndef REJIT_GLOBALS_H_
#define REJIT_GLOBALS_H_

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <iostream>
#include <algorithm>

#include "rejit.h"
#include "checks.h"
#include "flags.h"
#include "cpu.h"

using namespace std;  // NOLINT

namespace rejit {
namespace internal {

const int KB = 1024;
const int MB = KB * KB;

const uint32_t kMaxUInt = 0xffffffff;
const uint64_t kMaxUInt64 = 0xffffffffffffffffULL;

const int kPointerSize = sizeof(void*);  // NOLINT
const int kPointerSizeLog2 = 3;
const int kBitsPerByte = 8;
const int kBitsPerPointer = kPointerSize * kBitsPerByte;
const int kCharSize = sizeof(char);  // NOLINT
const int kBitsPerChar = 8;
const int kCharsPerPointer = kPointerSize / kCharSize;

typedef unsigned char byte;
typedef byte* Address;


#define DISALLOW_COPY_AND_ASSIGN(Type) \
  Type(const Type&);                   \
  void operator=(const Type&)

// Use this to avoid unused variable warnings.
template <typename T>
void USE(T) {}

// FUNCTION_ADDR(f) gets the address of a C function f.
#define FUNCTION_ADDR(f)                                        \
  (reinterpret_cast<rejit::internal::Address>(reinterpret_cast<intptr_t>(f)))

// FUNCTION_CAST<F>(addr) casts an address into a function
// of type F. Used to invoke generated code from within C.
template <typename F>
F FUNCTION_CAST(Address addr) {
  return reinterpret_cast<F>(reinterpret_cast<intptr_t>(addr));
}

} }  // namespace rejit::internal

#endif  // REJIT_GLOBALS_H_

