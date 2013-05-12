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

#ifndef REJIT_PARSER_H_
#define REJIT_PARSER_H_

#include "regexp.h"

namespace rejit {
namespace internal {

// TODO: The indexer and ff_finder should be merged into the parser.

// Regular expression parser.
class Parser {
 public:
  Parser() {}

  // TODO: Full support for ERE. Expose BRE support. More syntaxes
  enum Syntax {
    BRE,
    ERE
  };

  // Top level function to parse a regular expression.
  inline void Parse(Syntax syntax, RegexpInfo* rinfo, const char* regexp) {
    syntax_ = syntax;
    switch(syntax) {
      case BRE:
        ParseBRE(rinfo, regexp);
        break;
      case ERE:
        ParseERE(rinfo, regexp);
        break;
      default:
        UNREACHABLE();
    }
  }

  void ParseERE(RegexpInfo* rinfo, const char* regexp);
  void ParseBRE(RegexpInfo* rinfo, const char* regexp);

  int ParseCurlyBrackets(const char* left_curly_bracket);
  int ParseBrackets(const char* left_bracket);

  // Do/Push functions -----------------------------------------------
  // 'Push' functions' main purpose is to push something on the stack.
  // 'Do' functions execute more complex operations.
  // TODO: Give more details for each function.
  void PushChar(char c, bool append_to_mc_tos = true);
  void PushChar(const char* char_address);
  void PushPeriod();
  void PushAlternateBar();
  void PushLeftParenthesis();
  void PushAsterisk();
  void PushPlus();
  void PushQuestionMark();

  void DoRightParenthesis();
  void DoAlternateBar();

  void DoConcatenation();
  void DoAlternation();

  void DoFinish();



  // Stack access helpers --------------------------------------------
  // tos = top of stack.
  inline Regexp* tos() { return stack()->size() ? stack()->back() : NULL; }

  void PushRegexp(Regexp* regexp) {
    stack()->push_back(regexp);
  }

  Regexp* PopRegexp() {
    Regexp* r = stack()->back();
    stack()->pop_back();
    return r;
  }


  bool IsRetroactiveChar(char c) {
    if (c == '*') return true;
    if (c == '{') return true;
    return false;
  }

  void PrintStack() {
    cout << "---------------------------" << endl;
    vector<Regexp*>::iterator it;
    for (it = stack()->begin(); it < stack()->end(); it++) {
      cout << **it << endl;
    }
  }

  // Helpers ---------------------------------------------------------
  uint32_t ParseIntegerAt(const char* pos, char** end);

  // Error signaling -------------------------------------------------
  void ParseError(const char* pos, const char* format, ...);

  void Unexpected(const char* pos);

  inline void Unexpected(unsigned index) {
    Unexpected(regexp_string_ + index);
  }

  void Expected(const char* pos, const char *expected);

  inline void Expected(unsigned index, const char *expected) {
    Expected(regexp_string_ + index, expected);
  }

  void Expect(const char *c, const char* expected);


  RegexpInfo* regexp_info() { return regexp_info_; }
  vector<Regexp*>* stack() { return regexp_info()->regexp_tree(); }

 private:
  RegexpInfo* regexp_info_;
  const char* regexp_string_;
  uint64_t index_;
  Syntax syntax_;
};

} }  // namespace rejit::internal

#endif  // REJIT_PARSER_H_

