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

#include <map>

namespace rejit {
namespace internal {


class AssemblerBase;


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


// -----------------------------------------------------------------------------
// Data relocation
// Data may not fit the immediate fields of the instructions, and may not be
// convenient to generate on the fly.
// Code generation uses very little relocation and does not have as complex
// requirements as v8, so we use a simpler relocation system.
class RelocatedData {
 public:
  // The relocated value can be registered in an assembler that will take care
  // of freeing it at the same time it is destroyed.
  // The relocated value must be aligned according to the mask.
  RelocatedData(char *buf, size_t buf_size, bool copy_buf,
                unsigned alignment);
  ~RelocatedData();

  inline size_t buffer_size() const { return buffer_size_; }
  inline unsigned alignment() const { return alignment_; }

 private:
  char *buffer_;
  size_t buffer_size_;
  bool own_buffer_;
  unsigned alignment_;

  friend class AssemblerBase;
};

class RelocatedValue {
 public:
  RelocatedValue() :
      data_(NULL), offset_(0) {}

  RelocatedValue(RelocatedData *data, unsigned offset) :
      data_(data), offset_(offset) { ASSERT(offset_ >= 0); }

  inline RelocatedData *data() const { return data_; }

  bool operator<(const RelocatedValue& other) const {
    return data_ < other.data_;
  }

 private:
  RelocatedData *data_;
  // Offset from the base of the data.
  int32_t offset_;

  friend class AssemblerBase;
  friend class Assembler;
};


// -----------------------------------------------------------------------------
// Platform independent assembler base class.

class AssemblerBase {
  // TODO(rames): The AssemblerBase should manage the code generation buffer.
 public:
  explicit AssemblerBase(size_t min_buffer_size, size_t max_buffer_size,
                         unsigned max_instr_size,
                         void* buffer, int buffer_size);
  ~AssemblerBase();

  // Code buffer management --------------------------------
  VirtualMemory* GetCode();

  void GrowBuffer();

  inline int pc_offset() const { return static_cast<int>(pc_ - buffer_); }

  // Check if there is less than kGap bytes available in the buffer.
  // If this is the case, we need to grow the buffer before emitting
  // an instruction or relocation information.
  inline bool buffer_overflow() const {
    return pc_ >= buffer_ + buffer_size_ - max_instr_size_;
  }

  // Get the number of bytes available in the buffer.
  inline int available_space() const {
    return static_cast<int>(buffer_ + buffer_size_ - pc_);
  }

  byte byte_at(int pos)  { return buffer_[pos]; }
  void set_byte_at(int pos, byte value) { buffer_[pos] = value; }


  // Relocation --------------------------------------------
  RelocatedData *NewRelocatedData(char *buf, size_t buf_size, bool copy_buf,
                                  unsigned alignment);
  void EmitRelocData();

  void UseRelocatedData(RelocatedData *data);
  void UseRelocatedValue(RelocatedValue reloc);


 protected:
  // Architecture specific values.
  const size_t min_buffer_size_;
  const size_t max_buffer_size_;
  // Used to check that the buffer is big enough before assembling an
  // instruction.
  const int max_instr_size_;

  // Code buffer:
  // The buffer into which code is generated.
  byte* buffer_;
  size_t buffer_size_;
  // True if the assembler owns the buffer, false if buffer is external.
  bool own_buffer_;
  // code generation
  byte* pc_;  // the program counter; moves forward

  vector<RelocatedData*> reloc_data_owned_;
  // The relocated data used by this assembler and the offset at which the data
  // has been emitted in the buffer.
  map<RelocatedData*, int> reloc_data_location_;
  // The relocated values and the locations (offsets) at which they are used.
  map<RelocatedValue, int> reloc_values_usage_location_;
};

} }  // namespace rejit::internal

#endif  // REJIT_ASSEMBLER_BASE_H_
