// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2012 the V8 project authors. All rights reserved.

#ifndef REJIT_ASSEMBLER_BASE_H_
#define REJIT_ASSEMBLER_BASE_H_

#include "globals.h"
#include "regexp.h"

namespace rejit {
namespace internal {

// -----------------------------------------------------------------------------
// Platform independent assembler base class.

class AssemblerBase {
  // TODO(rames): The AssemblerBase should manage the code generation buffer.
 public:
  explicit AssemblerBase();
  ~AssemblerBase();

  VirtualMemory* GetCode();

 private:
  byte* codegen_buffer_;
};


// -----------------------------------------------------------------------------
// Labels represent pc locations; they are typically jump or call targets.
// After declaration, a label can be freely used to denote known or (yet)
// unknown pc location. Assembler::bind() is used to bind a label to the
// current pc. A label can be bound only once.

class Label {
 public:
  enum Distance {
    kNear, kFar
  };

  inline Label() {
    Unuse();
    UnuseNear();
  }

  inline ~Label() {
    ASSERT(!is_linked());
    ASSERT(!is_near_linked());
  }

  inline void Unuse() { pos_ = 0; }
  inline void UnuseNear() { near_link_pos_ = 0; }

  inline bool is_bound() const { return pos_ <  0; }
  inline bool is_unused() const { return pos_ == 0 && near_link_pos_ == 0; }
  inline bool is_linked() const { return pos_ >  0; }
  inline bool is_near_linked() const { return near_link_pos_ > 0; }

  // Returns the position of bound or linked labels. Cannot be used
  // for unused labels.
  int pos() const;
  int near_link_pos() const { return near_link_pos_ - 1; }

 private:
  // pos_ encodes both the binding state (via its sign)
  // and the binding position (via its value) of a label.
  //
  // pos_ <  0  bound label, pos() returns the jump target position
  // pos_ == 0  unused label
  // pos_ >  0  linked label, pos() returns the last reference position
  int pos_;

  // Behaves like |pos_| in the "> 0" case, but for near jumps to this label.
  int near_link_pos_;

  void bind_to(int pos)  {
    pos_ = -pos - 1;
    ASSERT(is_bound());
  }
  void link_to(int pos, Distance distance = kFar) {
    if (distance == kNear) {
      near_link_pos_ = pos + 1;
      ASSERT(is_near_linked());
    } else {
      pos_ = pos + 1;
      ASSERT(is_linked());
    }
  }

  friend class Assembler;
  friend class RegexpAssembler;
  friend class Displacement;
  friend class RegExpMacroAssemblerIrregexp;
};


} }  // namespace rejit::internal

#endif  // REJIT_ASSEMBLER_BASE_H_
