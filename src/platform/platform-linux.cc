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

// Platform specific code for POSIX goes here. This is not a platform on its
// own but contains the parts which are the same across POSIX platforms Linux,
// Mac OS, FreeBSD and OpenBSD.

#include "platform-posix.h"

#include <unistd.h>
#include <errno.h>
#include <time.h>

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include "platform.h"
#include "utils.h"

namespace rejit {
namespace internal {

size_t OS::AllocateAlignment() {
  return sysconf(_SC_PAGESIZE);
}


uint64_t OS::CpuFeaturesImpliedByPlatform() {
  return 0;  // Linux runs on anything.
}


void OS::Free(void* address, const size_t size) {
  // TODO(1240712): munmap has a return value which is ignored here.
  int result = munmap(address, size);
  USE(result);
  ASSERT(result == 0);
}


intptr_t OS::CommitPageSize() {
  static intptr_t page_size = getpagesize();
  return page_size;
}


// Get rid of writable permission on code allocations.
void OS::ProtectCode(void* address, const size_t size) {
  mprotect(address, size, PROT_READ | PROT_EXEC);
}


// Create guard pages.
void OS::Guard(void* address, const size_t size) {
  mprotect(address, size, PROT_NONE);
}
void* OS::GetRandomMmapAddr() {
  // TODO(rames): Implement this.
  // UNIMPLEMENTED();
  return NULL;
}


// Constants used for mmap.
static const int kMmapFd = -1;
static const int kMmapFdOffset = 0;

VirtualMemory::VirtualMemory() : address_(NULL), size_(0) { }

VirtualMemory::VirtualMemory(size_t size) {
  address_ = ReserveRegion(size);
  size_ = size;
}


VirtualMemory::VirtualMemory(size_t size, size_t alignment)
    : address_(NULL), size_(0) {
  ASSERT(IsAligned(alignment, static_cast<intptr_t>(OS::AllocateAlignment())));
  size_t request_size = RoundUp(size + alignment,
                                static_cast<intptr_t>(OS::AllocateAlignment()));
  void* reservation = mmap(OS::GetRandomMmapAddr(),
                           request_size,
                           PROT_NONE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                           kMmapFd,
                           kMmapFdOffset);
  if (reservation == MAP_FAILED) return;

  Address base = static_cast<Address>(reservation);
  Address aligned_base = RoundUp(base, alignment);
  ASSERT(base <= aligned_base);

  // Unmap extra memory reserved before and after the desired block.
  if (aligned_base != base) {
    size_t prefix_size = static_cast<size_t>(aligned_base - base);
    OS::Free(base, prefix_size);
    request_size -= prefix_size;
  }

  size_t aligned_size = RoundUp(size, OS::AllocateAlignment());
  ASSERT(aligned_size <= request_size);

  if (aligned_size != request_size) {
    size_t suffix_size = request_size - aligned_size;
    OS::Free(aligned_base + aligned_size, suffix_size);
    request_size -= suffix_size;
  }

  ASSERT(aligned_size == request_size);

  address_ = static_cast<void*>(aligned_base);
  size_ = aligned_size;
}


VirtualMemory::~VirtualMemory() {
  if (IsReserved()) {
    bool result = ReleaseRegion(address(), size());
    ASSERT(result);
    USE(result);
  }
}


bool VirtualMemory::IsReserved() {
  return address_ != NULL;
}


void VirtualMemory::Reset() {
  address_ = NULL;
  size_ = 0;
}


bool VirtualMemory::Commit(void* address, size_t size, bool is_executable) {
  return CommitRegion(address, size, is_executable);
}


bool VirtualMemory::Uncommit(void* address, size_t size) {
  return UncommitRegion(address, size);
}


bool VirtualMemory::Guard(void* address) {
  OS::Guard(address, OS::CommitPageSize());
  return true;
}


void* VirtualMemory::ReserveRegion(size_t size) {
  void* result = mmap(OS::GetRandomMmapAddr(),
                      size,
                      PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                      kMmapFd,
                      kMmapFdOffset);

  if (result == MAP_FAILED) return NULL;

  return result;
}


bool VirtualMemory::CommitRegion(void* base, size_t size, bool is_executable) {
  int prot = PROT_READ | PROT_WRITE | (is_executable ? PROT_EXEC : 0);
  if (MAP_FAILED == mmap(base,
                         size,
                         prot,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                         kMmapFd,
                         kMmapFdOffset)) {
    return false;
  }

  //UpdateAllocatedSpaceLimits(base, size);
  return true;
}


bool VirtualMemory::UncommitRegion(void* base, size_t size) {
  return mmap(base,
              size,
              PROT_NONE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED,
              kMmapFd,
              kMmapFdOffset) != MAP_FAILED;
}


bool VirtualMemory::ReleaseRegion(void* base, size_t size) {
  return munmap(base, size) == 0;
}


} }  // namespace rejit::internal
