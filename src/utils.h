// Copyright (C) 2013 Alexandre Rames <alexandre@coreperf.com>
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

#ifndef REJIT_UTILS_H
#define REJIT_UTILS_H

#include <stdint.h>
#include <string>
#include "globals.h"

using namespace std;  // NOLINT

namespace rejit {
namespace internal {

#define STATUS_STRING_SIZE 200

// Indentation helpers ---------------------------------------------------------
extern int IndentationLevel;

class IndentScope {
 public:
  IndentScope(int _indent = 1) : indent(_indent) {
    IndentationLevel = max(0, IndentationLevel + indent);
  }
  ~IndentScope() {
    IndentationLevel = max(0, IndentationLevel - indent);
  }
 private:
  int indent;
};

inline ostream& Indent(ostream& stream) {
  return stream << string(IndentationLevel, ' ');
}


// Addressing ------------------------------------------------------------------

template <typename T, typename U>
inline bool IsAligned(T value, U alignment) {
  return (value & (alignment - 1)) == 0;
}

// Compute the 0-relative offset of some absolute value x of type T.
// This allows conversion of Addresses and integral types into
// 0-relative int offsets.
template <typename T>
inline intptr_t OffsetFrom(T x) {
  return x - static_cast<T>(0);
}

// Compute the absolute value of type T for some 0-relative offset x.
// This allows conversion of 0-relative int offsets into Addresses and
// integral types.
template <typename T>
inline T AddressFrom(intptr_t x) {
  return static_cast<T>(static_cast<T>(0) + x);
}

// The type-based aliasing rule allows the compiler to assume that pointers of
// different types (for some definition of different) never alias each other.
// Thus the following code does not work:
//
// float f = foo();
// int fbits = *(int*)(&f);
//
// The compiler 'knows' that the int pointer can't refer to f since the types
// don't match, so the compiler may cache f in a register, leaving random data
// in fbits.  Using C++ style casts makes no difference, however a pointer to
// char data is assumed to alias any other pointer.  This is the 'memcpy
// exception'.
//
// Bit_cast uses the memcpy exception to move the bits from a variable of one
// type of a variable of another type.  Of course the end result is likely to
// be implementation dependent.  Most compilers (gcc-4.2 and MSVC 2005)
// will completely optimize BitCast away.
//
// There is an additional use for BitCast.
// Recent gccs will warn when they see casts that may result in breakage due to
// the type-based aliasing rule.  If you have checked that there is no breakage
// you can use BitCast to cast one pointer type to another.  This confuses gcc
// enough that it can no longer see that you have cast one pointer type to
// another thus avoiding the warning.

// We need different implementations of BitCast for pointer and non-pointer
// values. We use partial specialization of auxiliary struct to work around
// issues with template functions overloading.
template <class Dest, class Source>
struct BitCastHelper {
  STATIC_ASSERT(sizeof(Dest) == sizeof(Source));

  inline static Dest cast(const Source& source) {
    Dest dest;
    memcpy(&dest, &source, sizeof(dest));
    return dest;
  }
};

template <class Dest, class Source>
struct BitCastHelper<Dest, Source*> {
  inline static Dest cast(Source* source) {
    return BitCastHelper<Dest, uintptr_t>::
        cast(reinterpret_cast<uintptr_t>(source));
  }
};

template <class Dest, class Source>
inline Dest BitCast(const Source& source);

template <class Dest, class Source>
inline Dest BitCast(const Source& source) {
  return BitCastHelper<Dest, Source>::cast(source);
}

//#define offsetof(type, member) __builtin_offsetof(type, member)


// Arithmetic ------------------------------------------------------------------
// TODO(rames): Use good implementations for all arithmetic utils.

uint64_t FirstBytesMask(int n);
inline uint64_t FirstCharsMask(int n) { return FirstBytesMask(n * kCharSize); }

// Returns true iff x a power of 2. Cannot be used with the
// maximally negative value of the type T (the -1 overflows).
template <typename T>
inline bool IsPowerOf2(T x) {
  return x && ((x & (x - 1)) == 0);
}

inline unsigned WhichPowerOf2(uint64_t x) {
  ASSERT(x && IsPowerOf2(x));
  unsigned power = 0;
  power += (x >= (1ULL << 32)) * 32;
  power += (x >= (1ULL << 16)) * 16;
  power += (x >= (1ULL <<  8)) *  8;
  power += (x >= (1ULL <<  4)) *  4;
  power += (x >= (1ULL <<  2)) *  2;
  power += (x >= (1ULL <<  1)) *  1;
  return power;
}

inline unsigned NumberOfBitsSet(uint64_t x) {
  return __builtin_popcountl(x);
}

// Return the largest multiple of m which is <= x.
template <typename T>
inline T RoundDown(T x, intptr_t m) {
  ASSERT(IsPowerOf2(m));
  return AddressFrom<T>(OffsetFrom(x) & -m);
}

// Return the smallest multiple of m which is >= x.
template <typename T>
inline T RoundUp(T x, intptr_t m) {
  return RoundDown<T>(static_cast<T>(x + m - 1), m);
}

#define REPEAT_1_T0_63(M) \
  M(1) M(2) M(3) M(4) M(5) M(6) M(7) M(8) M(9) M(10) M(11) M(12) M(13) M(14) \
  M(15) M(16) M(17) M(18) M(19) M(20) M(21) M(22) M(23) M(24) M(25) M(26)    \
  M(27) M(28) M(29) M(30) M(31) M(32) M(33) M(34) M(35) M(36) M(37) M(38)    \
  M(39) M(40) M(41) M(42) M(43) M(44) M(45) M(46) M(47) M(48) M(49) M(50)    \
  M(51) M(52) M(53) M(54) M(55) M(56) M(57) M(58) M(59) M(60) M(61) M(62)    \
  M(63)

inline bool is_intn(int64_t x, unsigned n) {
  return -(1LL << (n-1)) <= x && x < (1LL << (n-1));
}

#define DEFINE_IS_INTN(n) \
  inline bool is_int##n(int64_t x) { return is_intn(x, n); }
REPEAT_1_T0_63(DEFINE_IS_INTN)
#undef DEFINE_IS_INTN

inline bool is_uintn(int64_t x, int n) {
  return (x & -(1LL << n)) == 0;
}

#define DEFINE_IS_UINTN(n) \
  inline bool is_uint##n(int64_t x) { return is_uintn(x, n); }
REPEAT_1_T0_63(DEFINE_IS_UINTN)
#undef DEFINE_IS_UINTN

#undef REPEAT_1_T0_63

// Range iterators -------------------------------------------------------------
template <typename Iterator>
class iterator_pair {
 public:
  iterator_pair(Iterator begin, Iterator end) : begin_(begin), end_(end) {}

  Iterator begin() const { return begin_; }
  Iterator end() const { return end_; }

 private:
  Iterator begin_;
  Iterator end_;
};

template <typename Iterator>
iterator_pair<Iterator> range(Iterator begin, Iterator end) {
  return iterator_pair<Iterator>(begin, end);
}

template <typename Iterator>
iterator_pair<Iterator> range(pair<Iterator, Iterator> it_pair) {
  return iterator_pair<Iterator>(it_pair.first, it_pair.second);
}

} }  // namespace rejit::internal

#endif  // REJIT_UTILS_H

