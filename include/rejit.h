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

// rejit headers.

#ifndef REJIT_H_
#define REJIT_H_

#include <vector>

namespace rejit {

void RejitTest();

// A match result.
//
// Upon match,
// begin points to the first character of the match.
// end points to
//  - the first character of the match if the match has a length of 0.
//  - one character past the last character of the match otherwise.
//
// For example:
//  - When matching '^$' in an empty string "", the first (and only) match
//  will have both begin and end pointing at the end of string marker.
//  - When matching 'abc in an empty string "0abc1", the first (and only) match
//  will have begin pointing at '0' and end pointing at '1'.
struct Match {
  const char* begin;
  const char* end;
};

// Ordered by matching 'difficulty'.
enum MatchType {
  kMatchFull,
  kMatchAnywhere,
  kMatchFirst,
  kMatchAll,
  kNMatchTypes
};

namespace internal  {
// Internal structure used to track compilation information.
// A forward declaration is required here to reference it from class Regej.
class RegexpInfo;
}

class Regej {
 public:
  explicit Regej(const char* regexp);
  ~Regej();

  // TODO(rames): Add table with examples.

  // Returns true iff the regexp matches the whole string.
  bool MatchFull(const char* string);
  // Returns true if there is a match in the string.
  bool MatchAnywhere(const char* string);
  // Find the left-most longest match in string. Returns true if there is a
  // match.
  bool MatchFirst(const char* string, Match* match);
  // Returns a list of all left-most longest matches.
  void MatchAll(const char*, std::vector<struct Match>* matches);

  bool Compile(MatchType match_type);

 private:
  char const * const regexp_;
  // This refers to internal compilation information.
  internal::RegexpInfo* rinfo_;
};

}  // namespace rejit

#endif  // REJIT_H_
