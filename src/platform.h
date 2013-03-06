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

// This module contains the platform-specific code. This make the rest of the
// code less dependent on operating system, compilers and runtime libraries.
// This module does specifically not deal with differences between different
// processor architecture.
// The platform classes have the same definition for all platforms. The
// implementation for a particular platform is put in platform_<os>.cc.
// The build system then uses the implementation for the target platform.
//
// This design has been chosen because it is simple and fast. Alternatively,
// the platform dependent classes could have been implemented using abstract
// superclasses with virtual methods and having specializations for each
// platform. This design was rejected because it was more complicated and
// slower. It would require factory methods for selecting the right
// implementation and the overhead of virtual methods for performance
// sensitive like mutex locking/unlocking.

#ifndef REJIT_PLATFORM_H_
#define REJIT_PLATFORM_H_


#include "globals.h"

namespace rejit {
namespace internal {

// Use AtomicWord for a machine-sized pointer. It is assumed that
// reads and writes of naturally aligned values of this type are atomic.
typedef intptr_t AtomicWord;

// ----------------------------------------------------------------------------
// OS
//
// This class has static methods for the different platform specific
// functions. Add methods here to cope with differences between the
// supported platforms.

class OS {
 public:
  // Allocate/Free memory. Pages are readable/writable, but
  // they are not guaranteed to be executable unless 'executable' is true.
  // Returns the address of allocated memory, or NULL if failed.
  static void* Allocate(const size_t requested,
                        size_t* allocated,
                        bool is_executable);
  static void Free(void* address, const size_t size);

  // This is the granularity at which the ProtectCode(...) call can set page
  // permissions.
  static intptr_t CommitPageSize();

  // Mark code segments non-writable.
  static void ProtectCode(void* address, const size_t size);

  // Assign memory as a guard page so that access will cause an exception.
  static void Guard(void* address, const size_t size);

  // Generate a random address to be used for hinting mmap().
  static void* GetRandomMmapAddr();

  // Get the Alignment guaranteed by Allocate().
  static size_t AllocateAlignment();

  // Returns an indication of whether a pointer is in a space that
  // has been allocated by Allocate().  This method may conservatively
  // always return false, but giving more accurate information may
  // improve the robustness of the stack dump code in the presence of
  // heap corruption.
  static bool IsOutsideAllocatedSpace(void* pointer);

  // Sleep for a number of milliseconds.
  static void Sleep(const int milliseconds);

  // Abort the current process.
  static void Abort();

  // Debug break.
  static void DebugBreak();

  // The return value indicates the CPU features we are sure of because of the
  // OS.  For example MacOSX doesn't run on any x86 CPUs that don't have SSE2
  // instructions.
  // This is a little messy because the interpretation is subject to the cross
  // of the CPU and the OS.  The bits in the answer correspond to the bit
  // positions indicated by the members of the CpuFeature enum from globals.h
  static uint64_t CpuFeaturesImpliedByPlatform();

  // Maximum size of the virtual memory.  0 means there is no artificial
  // limit.
  static intptr_t MaxVirtualMemory();

  // Returns the activation frame alignment constraint or zero if
  // the platform doesn't care. Guaranteed to be a power of two.
  static int ActivationFrameAlignment();

  static void ReleaseStore(volatile AtomicWord* ptr, AtomicWord value);
};

// Represents and controls an area of reserved memory.
// Control of the reserved memory can be assigned to another VirtualMemory
// object by assignment or copy-contructing. This removes the reserved memory
// from the original object.
class VirtualMemory {
 public:
  // Empty VirtualMemory object, controlling no reserved memory.
  VirtualMemory();

  // Reserves virtual memory with size.
  explicit VirtualMemory(size_t size);

  // Reserves virtual memory containing an area of the given size that
  // is aligned per alignment. This may not be at the position returned
  // by address().
  VirtualMemory(size_t size, size_t alignment);

  // Releases the reserved memory, if any, controlled by this VirtualMemory
  // object.
  ~VirtualMemory();

  // Returns whether the memory has been reserved.
  bool IsReserved();

  // Initialize or resets an embedded VirtualMemory object.
  void Reset();

  // Returns the start address of the reserved memory.
  // If the memory was reserved with an alignment, this address is not
  // necessarily aligned. The user might need to round it up to a multiple of
  // the alignment to get the start of the aligned block.
  void* address() {
    ASSERT(IsReserved());
    return address_;
  }

  // Returns the size of the reserved memory. The returned value is only
  // meaningful when IsReserved() returns true.
  // If the memory was reserved with an alignment, this size may be larger
  // than the requested size.
  size_t size() { return size_; }

  // Commits real memory. Returns whether the operation succeeded.
  bool Commit(void* address, size_t size, bool is_executable);

  // Uncommit real memory.  Returns whether the operation succeeded.
  bool Uncommit(void* address, size_t size);

  // Creates a single guard page at the given address.
  bool Guard(void* address);

  void Release() {
    ASSERT(IsReserved());
    // Notice: Order is important here. The VirtualMemory object might live
    // inside the allocated region.
    void* address = address_;
    size_t size = size_;
    Reset();
    bool result = ReleaseRegion(address, size);
    USE(result);
    ASSERT(result);
  }

  // Assign control of the reserved region to a different VirtualMemory object.
  // The old object is no longer functional (IsReserved() returns false).
  void TakeControl(VirtualMemory* from) {
    ASSERT(!IsReserved());
    address_ = from->address_;
    size_ = from->size_;
    from->Reset();
  }

  static void* ReserveRegion(size_t size);

  static bool CommitRegion(void* base, size_t size, bool is_executable);

  static bool UncommitRegion(void* base, size_t size);

  // Must be called with a base pointer that has been returned by ReserveRegion
  // and the same size it was reserved with.
  static bool ReleaseRegion(void* base, size_t size);

 private:
  void* address_;  // Start address of the virtual memory.
  size_t size_;  // Size of the virtual memory.
};


} }  // namespace rejit::internal

#endif  // REJIT_PLATFORM_H_
