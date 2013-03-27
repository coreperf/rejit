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

using namespace std;

namespace rejit {

// A match result.
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

// High level helpers.
// These are convenient helpers that abstract the use of the Regej class below.
// However rejit currently does not yet have a cache for compiled regular
// expressions. So running these helpers many times will be less efficient than
// keeping using the Regej methods.
// TODO(rames): Add table with examples.

// Returns true iff the regexp matches the whole string.
bool MatchFull(const char* regexp, const char* string);
// Returns true if there is a match in the text.
bool MatchAnywhere(const char* regexp, const char* text);
// Find the left-most longest match in the text. Returns true if there is a
// match, and false otherwise.
bool MatchFirst(const char* regexp, const char* text, Match* match);
// Returns a list of all left-most longest matches.
void MatchAll(const char* regexp, const char* text,
              std::vector<struct Match>* matches);
// Count the number of left-most longest matches in the text.
size_t MatchAllCount(const char* regexp, const char* text);

// TODO: Provide more convenient function signatures with strings.
// Replace one or multiple matches in text.
// The replacement functions allocate a new buffer which is filled with the
// text and replaced matches and then returned. It is the caller's
// responsibility to free the returned buffer.
char* Replace(const char* text,
              Match to_replace, const char* with,
              size_t text_size = 0, size_t with_size = 0);
char* Replace(const char* text,
              vector<Match>* to_replace, const char* with,
              size_t text_size = 0, size_t with_size = 0);
// This is equivalent to MatchAll followed by Replace.
// It is the caller's responsibility to free the returned buffer.
char* ReplaceAll(const char* regexp, const char* text, const char* with,
                 size_t text_size = 0, size_t with_size = 0);

// Types of matches. 
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

  // For details about following functions, see global functions above for.

  // Matching functions can operate on char pointers without needing to know the
  // size of the text.
  bool MatchFull(const char* text);
  bool MatchAnywhere(const char* text);
  bool MatchFirst(const char* text, Match* match);
  void MatchAll(const char*, std::vector<struct Match>* matches);
  size_t MatchAllCount(const char* text);

  // This is equivalent to MatchAll followed by Replace.
  char* ReplaceAll(const char* text, const char* with,
                   size_t text_size = 0, size_t with_size = 0);

  bool Compile(MatchType match_type);

 private:
  char const * const regexp_;
  // This refers to internal compilation information.
  internal::RegexpInfo* rinfo_;
};

}  // namespace rejit

#endif  // REJIT_H_
