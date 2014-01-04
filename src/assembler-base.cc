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

#include "assembler-base.h"

#include "allocation.h"

#ifdef REJIT_TARGET_ARCH_X64
#else
#error "Unsupported architecture."
#endif

namespace rejit {
namespace internal {


// -----------------------------------------------------------------------------
// Implementation of Label

int Label::pos() const {
  if (pos_ < 0) return -pos_ - 1;
  if (pos_ > 0) return  pos_ - 1;
  UNREACHABLE();
  return 0;
}


RelocatedData::RelocatedData(char *buf, size_t buf_size, bool copy_buf,
                             unsigned alignment) {
  alignment_ = alignment;
  if (!copy_buf) {
    buffer_size_ = buf_size;
    buffer_ = buf;
    own_buffer_ = false;
  } else {
    buffer_ = NewArray<char>(buf_size);
    buffer_size_ = buf_size;
    own_buffer_ = true;
    memcpy(buffer_, buf, buf_size);
  }
}


RelocatedData::~RelocatedData() {
  if (own_buffer_) {
    delete buffer_;
  }
}


AssemblerBase::AssemblerBase(size_t min_buffer_size, size_t max_buffer_size,
                             unsigned max_instr_size,
                             void* buffer, int buffer_size) :
    min_buffer_size_(min_buffer_size), max_buffer_size_(max_buffer_size),
    max_instr_size_(max_instr_size)
{
  if (buffer == NULL) {
    // Do our own buffer management.
    if (buffer_size <= min_buffer_size_) {
      buffer_size = min_buffer_size_;
    }
    buffer_ = NewArray<byte>(buffer_size);
    buffer_size_ = buffer_size;
    own_buffer_ = true;
  } else {
    // Use externally provided buffer instead.
    ASSERT(buffer_size > 0);
    buffer_ = static_cast<byte*>(buffer);
    buffer_size_ = buffer_size;
    own_buffer_ = false;
  }

  // Set up buffer pointers.
  ASSERT(buffer_ != NULL);
  pc_ = buffer_;
}


AssemblerBase::~AssemblerBase() {
  if (own_buffer_) {
    delete[] buffer_;
  }
  for (RelocatedData* reloc : reloc_data_owned_) {
    delete reloc;
  }
}


// TODO(ajr): Clean and check memory type (writable?).
VirtualMemory* AssemblerBase::GetCode() {
  VirtualMemory* vmem = new VirtualMemory(pc_offset());
  if (!vmem->IsReserved()) {
    FATAL("VirtualMemory has not been reserved.");
    delete vmem;
    return NULL;
  }
  if (!vmem->Commit(vmem->address(), pc_offset(), true/*executable*/)) {
    FATAL("VirtualMemory has not been committed.");
    delete vmem;
    return NULL;
  }

  memcpy(vmem->address(), buffer_, pc_offset());

  // TODO(rames): Clean up how the code is returned. Returning virtual memory
  // is not very intuitive!
  return vmem;
}


void AssemblerBase::GrowBuffer() {
  ASSERT(buffer_overflow());
  if (!own_buffer_) {
    FATAL("external code buffer is too small");
  }

  byte* new_buffer;
  size_t new_buffer_size;

  ASSERT(buffer_size_ >= min_buffer_size_);
  if (buffer_size_ >= max_buffer_size_) {
    FATAL("The code generation buffer has exceeded its maximum possible size.");
  }
  new_buffer_size = min(2 * buffer_size_, max_buffer_size_);

  // Set up new buffer.
  new_buffer = NewArray<byte>(new_buffer_size);

  // Get the pc offset before switching buffers.
  ptrdiff_t offset = pc_offset();

  // Copy the data.
  memmove(new_buffer, buffer_, offset);
  // Switch buffers.
  delete[] buffer_;
  buffer_ = new_buffer;
  buffer_size_ = new_buffer_size;
  pc_ = new_buffer + offset;

  ASSERT(!buffer_overflow());
}


RelocatedData *AssemblerBase::NewRelocatedData(char *buf, size_t buf_size,
                                               bool copy_buf,
                                               unsigned alignment_mask) {
  RelocatedData *reloc = new RelocatedData(buf, buf_size,
                                           copy_buf, alignment_mask);
  reloc_data_owned_.push_back(reloc);
  return reloc;
}


void AssemblerBase::EmitRelocData() {
  // Emit relocation data that has not been emitted yet.
  //for (pair<RelocatedData*, int>& reloc_info : reloc_data_location_) {
  for (auto& reloc_info : reloc_data_location_) {
    if (reloc_info.second == -1) {
      RelocatedData* reloc = reloc_info.first;
      if (available_space() < reloc->buffer_size_ + reloc->alignment_) {
        GrowBuffer();
      }
      ASSERT(IsPowerOf2(reloc->alignment_));
      int alignment_mask = reloc->alignment_ - 1;
      pc_ += (reloc->alignment_ - (pc_offset() & alignment_mask)) & alignment_mask;
      memcpy(pc_, reloc->buffer_, reloc->buffer_size_);
      reloc_info.second = pc_offset();
      pc_ += reloc->buffer_size_;
    }
  }
}


void AssemblerBase::UseRelocatedData(RelocatedData *data) {
  map<RelocatedData*, int>::iterator it;
  it = reloc_data_location_.find(data);
  if (it == reloc_data_location_.end()) {
    reloc_data_location_.insert(pair<RelocatedData*, int>(data, -1));
  }
}


void AssemblerBase::UseRelocatedValue(RelocatedValue reloc) {
  UseRelocatedData(reloc.data_);
  reloc_values_usage_location_.insert(pair<RelocatedValue, int>(reloc, pc_offset()));
}


} }  // namespace rejit::internal
