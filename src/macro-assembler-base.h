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

#include "assembler.h"

namespace rejit {
namespace internal {

class MacroAssemblerBase : public Assembler {
 public:
  MacroAssemblerBase(void* buffer, int buffer_size)
    : Assembler(buffer, buffer_size), char_size_(kCharSize) {}

  inline unsigned char_size() const { return char_size_; }
  inline void set_char_size(unsigned size) { char_size_ = size; }

 private:
  unsigned char_size_;
};

enum Direction {
  kForward,
  kBackward
};


} }  // namespace rejit::internal

