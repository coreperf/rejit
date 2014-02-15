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

// rejit headers.

#ifndef REJIT_H_
#define REJIT_H_

#include <string>
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
//  - When matching 'abc' in string "0abc1", the first (and only) match
//  will have begin pointing at 'a' and end pointing at '1'.
//
// TODO: Consider using offsets rather than pointers in the Match structure.
struct Match {
  const char* begin;
  const char* end;
};

// High level helpers.
// These are convenient helpers that abstract the use of the Regej class below.
// Rejit currently does not yet have a cache for compiled regular expressions,
// so using the Regej class methods will be more efficient than calling these
// multiple times.
// TODO(rames): Add table with examples.

// Returns true iff the regexp matches the whole text.
bool MatchFull(const char* regexp, const string& text);
bool MatchFull(const char* regexp, const char* text, size_t text_size);
// Returns true if there is a match in the text.
bool MatchAnywhere(const char* regexp, const string& text);
bool MatchAnywhere(const char* regexp, const char* text, size_t text_size);
// Find the left-most longest match in the text. Returns true if there is a
// match, and false otherwise.
bool MatchFirst(const char* regexp, const string& text, Match* match);
bool MatchFirst(const char* regexp, const char* text, size_t text_size, Match* match);
// Fill the vector with all left-most longest matches.
// Returns the size of the vector for convenience.
size_t MatchAll(const char* regexp, const string& text,
                std::vector<struct Match>* matches);
size_t MatchAll(const char* regexp, const char* text, size_t text_size,
                std::vector<struct Match>* matches);
// Count the number of left-most longest matches in the text.
size_t MatchAllCount(const char* regexp, const string& text);
size_t MatchAllCount(const char* regexp, const char* text, size_t text_size);

// Replace one or multiple matches in text.
void Replace(Match to_replace, string& text, const string& with);
void Replace(vector<Match>* to_replace, string& text, const string& with);
// This is equivalent to MatchFirst/MatchAll followed by Replace.
// Returns the number of matches that were replaced.
bool ReplaceFirst(const char* regexp, string& text, const string& with);
size_t ReplaceAll(const char* regexp, string& text, const string& with);

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

// Error status returned by rejit.
// Upon error, the rejit_status_string is updated with an error message.
enum Status {
  RejitSuccess = 0,
  // Parser errors must have negative codes.
  ParserError = -1
};
extern char* const rejit_status_string;

class Regej {
 public:
  explicit Regej(const char* regexp);
  explicit Regej(const string& regexp);
  ~Regej();

  // Error codes used to indicate the status of the Regej.
  Status status() const { return status_; }

  // See related global functions above for details about the following
  // functions.
  bool MatchFull(const string& text);
  bool MatchFull(const char* text, size_t text_size);
  bool MatchAnywhere(const string& text);
  bool MatchAnywhere(const char* text, size_t text_size);
  bool MatchFirst(const string& text, Match* match);
  bool MatchFirst(const char* text, size_t text_size, Match* match);
  size_t MatchAll(const string& text, std::vector<struct Match>* matches);
  size_t MatchAll(const char* text, size_t text_size, std::vector<struct Match>* matches);
  size_t MatchAllCount(const string& text);
  size_t MatchAllCount(const char* text, size_t text_size);

  // This is equivalent to MatchFirst/MatchAll followed by Replace.
  bool ReplaceFirst(string& text, const string& with);
  size_t ReplaceAll(string& text, const string& with);

  bool Compile(MatchType match_type);

 private:
  char const * const regexp_;
  // This refers to internal compilation information.
  internal::RegexpInfo* rinfo_;
  Status status_;
};

}  // namespace rejit

#endif  // REJIT_H_
