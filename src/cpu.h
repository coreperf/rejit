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


#ifndef REJIT_CPU_H_
#define REJIT_CPU_H_

// TODO(rames): We should detect that at runtime.
#define MAX_PARALLEL_CHARS 4
static const int kMaxParallelChars = 4;
static const int kMaxParallelCharsLog2 = 2;

namespace rejit {
namespace internal {

// This is from v8/src/v8globals.h.
//
// Feature flags bit positions. They are mostly based on the CPUID spec.
// (We assign CPUID itself to one of the currently reserved bits --
// feel free to change this if needed.)
// On X86/X64, values below 32 are bits in EDX, values above 32 are bits in ECX.
enum CpuFeature {
  SSE4_2 = 32 + 20,  // x86
  SSE4_1 = 32 + 19,  // x86
  SSE3 = 32 + 0,     // x86
  SSE2 = 26,   // x86
  CMOV = 15,   // x86
  RDTSC = 4,   // x86
  CPUID = 10,  // x86
  VFP3 = 1,    // ARM
  ARMv7 = 2,   // ARM
  VFP2 = 3,    // ARM
  SAHF = 0,    // x86
  FPU = 1
};    // MIPS

// ----------------------------------------------------------------------------
// CPU
//
// This class has static methods for the architecture specific functions. Add
// methods here to cope with differences between the supported architectures.
//
// For each architecture the file cpu_<arch>.cc contains the implementation of
// these functions.

class CPU {
 public:
  // Initializes the cpu architecture support. Called once at VM startup.
  static void SetUp();

  static bool SupportsCrankshaft();

  // Flush instruction cache.
  static void FlushICache(void* start, size_t size);

  // Try to activate a system level debugger.
  static void DebugBreak();
};

} }  // namespace rejit::internal


#endif  // REJIT_CPU_H_

