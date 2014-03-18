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

#include "parser.h"
#include <iostream>
#include <stdlib.h>

namespace rejit {
namespace internal {

// TODO(rames): Compliance with ERE spec.

static char hex_code_from_char(char c) {
  if ('0' <= c && c <= '9') {
    return c - '0';
  }
  if ('A' <= c && c <= 'F') {
    return c - 'A';
  }
  if ('a' <= c && c <= 'f') {
    return c - 'a';
  }

  UNREACHABLE();
  return '\0';
}


Status Parser::ParseERE(RegexpInfo* rinfo, const char* regexp) {
  index_ = 0;

  char c;
  char lookahead;
  int advance = 0;

  while ((c = *(regexp_string_ + index_))) {
    lookahead = *(regexp_string_ + index_ + 1);
    // By default advance one character.
    advance = 1;

    switch (c) {
      case '\\': {
        advance = 2;
        switch (lookahead) {
          case '(':
          case ')':
          case '{':
          case '}':
          case '[':
          case ']':
          case '|':
          case '*':
          case '+':
          case '^':
          case '$':
          case '\\':
            PushChar(regexp_string_ + index_ + 1);
            break;

#ifdef ENABLE_COMMON_ESCAPED_PATTERNS
          case 'd': {
          case 'D':
            Bracket * bracket = new Bracket();
            bracket->AddCharRange({'0', '9'});
            rinfo->UpdateRegexpMaxLength(bracket);
            if (lookahead == 'D') {
              bracket->set_flag(Bracket::non_matching);
            }
            PushRegexp(bracket);
            break;
          }
          case 'n': {
            PushChar('\n');
            break;
          }
          case 's': {
          case 'S':
            Bracket * bracket = new Bracket();
            bracket->AddSingleChar(' ');
            bracket->AddSingleChar('\t');
            rinfo->UpdateRegexpMaxLength(bracket);
            if (lookahead == 'S') {
              bracket->set_flag(Bracket::non_matching);
            }
            PushRegexp(bracket);
            break;
          }
          case 't': {
            PushChar('\t');
            break;
          }
          case 'x': {
            advance = 4;
            char hex_char =
              hex_code_from_char(*(regexp_string_ + index_ + 2)) << 4 |
              hex_code_from_char(*(regexp_string_ + index_ + 3));
            PushChar(hex_char);
            break;
          }
#endif

          default:
            return Unexpected(index_ + 1);
            UNREACHABLE();
        }
        break;
      }

      case '{':
        advance = ParseCurlyBrackets(regexp_string_ + index_);
        break;

      case '.':
        PushPeriod();
        break;

      case '*':
        PushAsterisk();
        break;

      case '+':
        PushPlus();
        break;

      case '?':
        PushQuestionMark();
        break;

      case '^': {
        StartOfLine* sol = new StartOfLine();
        regexp_info()->UpdateRegexpMaxLength(sol);
        PushRegexp(sol);
        break;
      }

      case '$': {
        EndOfLine* eol = new EndOfLine();
        regexp_info()->UpdateRegexpMaxLength(eol);
        PushRegexp(eol);
        break;
      }

      case '(':
        PushLeftParenthesis();
        break;

      case ')':
        DoRightParenthesis();
        break;

      case '|':
        DoAlternateBar();
        break;


      case '[':
        advance = ParseBrackets(regexp_string_ + index_);
        break;
      case ']':
        UNREACHABLE();
        break;

      default:
        // Standard character.
        advance = 1;
        PushChar(regexp_string_ + index_);
    }

    if (status_ != RejitSuccess)
      return status_;

    index_ += advance;
  }

  DoFinish();

  // Check that the stack only contains the root node.
  ALWAYS_ASSERT(stack_.size() == 1);

  regexp_info()->set_regexp(stack_.at(0));


  return status_;
}


Status Parser::ParseBRE(RegexpInfo* rinfo, const char* regexp) {
  char c;
  char lookahead;
  size_t index = 0;
  int advance = 0;

  while ((c = *(regexp + index))) {
    lookahead = *(regexp + index + 1);

    switch (c) {
      case '\\': {
        advance = 2;
        switch (lookahead) {
          case '(':
            PushLeftParenthesis();
            break;

          case ')':
            DoRightParenthesis();
            break;

          case '|':
            DoAlternateBar();
            break;

          case 1:
          case 2:
          case 3:
          case 4:
          case 5:
          case 6:
          case 7:
          case 8:
          case 9:
            UNIMPLEMENTED();

          case '{':
            advance += ParseCurlyBrackets(regexp + index + 1);
            break;
          case '}':
            UNREACHABLE();
            break;

          // Special characters preceded by a backslash match the character.
          case '.':
          case '*':
          case '^':
          case '$':
            PushChar(regexp + index + 1);
            break;

          default:
            break;
        }
        break;
      }

      case '.':
        advance = 1;
        PushPeriod();
        break;

      case '*':
        advance = 1;
        PushAsterisk();
        break;

      case '^': {
        advance = 1;
        StartOfLine* sol = new StartOfLine();
        regexp_info()->UpdateRegexpMaxLength(sol);
        PushRegexp(sol);
        break;
      }

      case '$': {
        advance = 1;
        EndOfLine* eol = new EndOfLine();
        regexp_info()->UpdateRegexpMaxLength(eol);
        PushRegexp(eol);
        break;
      }

      case '[':
        advance = ParseBrackets(regexp + index);
        break;
      case ']':
        UNREACHABLE();
        break;

      default:
        // Standard character.
        advance = 1;
        PushChar(regexp + index);
    }

    index += advance;
  }

  DoFinish();

  // Check that the stack only contains the root node.
  ALWAYS_ASSERT(stack_.size() == 1);

  regexp_info()->set_regexp(stack_.at(0));

  return RejitSuccess;
}


uint32_t Parser::ParseIntegerAt(const char* pos, char** end) {
  uint32_t n = strtoul(pos, end, 10);
  if (pos == *end) {
    Expected(pos, "<base 10 integer>");
  }
  return n;
}


int Parser::ParseCurlyBrackets(const char *left_curly_bracket) {
  // Parse the min and max indexes.
  uint32_t min, max;
  const char* c = left_curly_bracket + 1;
  char* end;
  bool escaped_brackets = (syntax_ == BRE);

  if (*c == ',') {
    min = 0;
    c++;  // Skip the comma.
    max = ParseIntegerAt(c, &end);
    Expect(end, escaped_brackets ? "\\}" : "}");
    c = end + (escaped_brackets ? 2 : 1);

  } else {
    min = ParseIntegerAt(c, &end);
    c = end;
    if (*c == ',') {
      c++;  // Skip the ','.
      if ((escaped_brackets && ((*c == '\\') && (*(c + 1) == '}'))) ||
          (*c == '}')) {
        max = kMaxUInt;
        c += escaped_brackets ? 2 : 1;
      } else {
        max = ParseIntegerAt(c, &end);
        c = end;
        if (escaped_brackets) {
          Expect(c, "\\}");
          c += 2;
        } else {
          Expect(c, "}");
          c += 1;
        }
      }
    } else {
      if (escaped_brackets) {
        Expect(c, "\\}");
        c += 2;
      } else {
        Expect(c, "}");
        c += 1;
      }
      max = min;
    }
  }

  if (min > max) {
    const char* right_curly_bracket = c - (escaped_brackets ? 3 : 1);
    return ParseError(right_curly_bracket,
                      "Invalid repetition bounds: %u > %u\n",
                      min, max);
  }

  Regexp *re = PopRegexp();

  if (FLAG_use_parser_opt &&
      re->IsMultipleChar() && min > 1) {
    // Optimize a{min,max} to a^m a{0,max-min}

    Regexp *result = NULL;
    MultipleChar *mc = re->AsMultipleChar();
    MultipleChar *mc_start;

    if (min == max) {
      // We do not need to preserve the base regexp.
      mc_start = mc;
      result = mc_start;
    } else {
      mc_start = new MultipleChar(mc->chars());
    }

    uint32_t repeat_base = 1;
    int mc_base_len = mc->chars_length();
    vector<char>::iterator mc_base_begin = mc->chars_.begin();
    vector<char>::iterator mc_base_end = mc->chars_.end();

    Concatenation *concat = NULL;
    if (mc_base_len * min > kMaxNodeLength || min != max) {
      concat = new Concatenation();
      result = concat;
    }

    while (repeat_base++ < min) {
      if (mc_start->chars_length() + mc_base_len > kMaxNodeLength) {
        concat->Append(mc_start);
        mc_start = new MultipleChar();
      }
      mc_start->chars_.insert(mc_start->chars_.end(),
                              mc_base_begin, mc_base_end);
    }
    if (concat != NULL) {
      concat->Append(mc_start);
    }

    if (min != max) {
      concat->Append(
          new Repetition(mc, 0, max == kMaxUInt ? kMaxUInt : max - min));
    }

    regexp_info()->UpdateRegexpMaxLength(result);
    PushRegexp(result);

  } else {
    PushRegexp(new Repetition(re, min, max));
  }

  // c points after the closing bracket.
  return c - left_curly_bracket;
}


int Parser::ParseBrackets(const char *left_bracket) {
  // TODO(rames): Fully handle brackets.
  const char* c = left_bracket + 1;
  Bracket* bracket = new Bracket();
  if (*c == '^') {
    bracket->set_flag(Bracket::non_matching);
    c++;
  }
  if (*c == '-') {
    bracket->AddSingleChar('-');
    c++;
  }
  while (true) {
    if (*c == ']') {
      c++;
      break;
    }
    if (*(c + 1) == ']') {
      bracket->AddSingleChar(*c);
      c++;
    } else if (*(c + 2) == ']') {
      bracket->AddSingleChar(*c);
      bracket->AddSingleChar(*(c + 1));
      c += 2;
    } else if (*(c + 1) == '-') {
      Bracket::CharRange range = {*c, *(c + 2)};
      bracket->AddCharRange(range);
      c += 3;
    } else {
      bracket->AddSingleChar(*c);
      c++;
    }
  }
  regexp_info()->UpdateRegexpMaxLength(bracket);
  PushRegexp(bracket);
  return c - left_bracket;
}


void Parser::PushChar(char c, bool append_to_mc_tos) {
  MultipleChar* mc;

  if (append_to_mc_tos && tos() && tos()->IsMultipleChar()) {
    // Append the character to the MultipleCharacter regexp on the top of the
    // stack.
    mc = reinterpret_cast<MultipleChar*>(tos());
    if (!mc->IsFull()) {
      mc->PushChar(c);
      regexp_info()->UpdateRegexpMaxLength(mc);
      return;
    }
  }

  // Create a new mc.
  mc = new MultipleChar(c);
  regexp_info()->UpdateRegexpMaxLength(mc);
  PushRegexp(mc);
}


void Parser::PushChar(const char* char_address) {
  char lookahead = '\0';
  if (char_address && *char_address != '\0') {
    lookahead = *(char_address + 1);
  }

  PushChar(*char_address, !IsRetroactiveChar(lookahead));
}


void Parser::PushPeriod() {
  Period* dot = new Period();
  regexp_info()->UpdateRegexpMaxLength(dot);
  PushRegexp(dot);
}


void Parser::PushLeftParenthesis() {
  PushRegexp(new Regexp(kLeftParenthesis));
}


void Parser::DoRightParenthesis() {
  if (syntax_ == ERE) {
    // A right parenthesis not matched with a preceding left-parenthesis is not
    // a special character.
    vector<Regexp*>::reverse_iterator rit;
    for (rit = stack()->rbegin(); rit < stack()->rend(); rit++) {
      if ((*rit)->IsLeftParenthesis()) {
        break;
      }
    }
    if (rit >= stack()->rend()) {
      // There is no matching left-parenthesis.
      PushChar(regexp_string_ + index_);
      return;
    }
  }
  DoAlternation();
  Regexp* concat = PopRegexp();
  ASSERT(tos()->type() == kLeftParenthesis);
  PopRegexp();
  PushRegexp(concat);
}


void Parser::DoAlternateBar() {
  DoConcatenation();
  PushAlternateBar();
}


void Parser::PushAlternateBar() {
  PushRegexp(new Regexp(kAlternateBar));
}


void Parser::DoConcatenation() {
  Concatenation* concat;
  vector<Regexp*>::iterator it;
  // Go down the stack until we find a marker or the beginning of the stack.
  vector<Regexp*>::iterator end = stack()->end();
  vector<Regexp*>::iterator first;
  for (it = end - 1;
       it > stack()->begin() && !(*it)->IsMarker(); it--) {
    // TODO(rames): Optimisation stuff here.
  }
  // Only concatenate two or more elements.
  first = (*it)->IsMarker() ? it + 1 : it;
  //if (first + 1 != end) {
  //  concat = new Concatenation();
  //  concat->sub_regexps()->assign(first, end);
  //  stack()->erase(first, end);
  //  PushRegexp(concat);
  //}
  if (first + 1 != end) {
    concat = new Concatenation();
    for (it = first; it < end; it++) {
      concat->Append(*it);
    }
    stack()->erase(first, end);
    PushRegexp(concat);
  }
}


void Parser::DoAlternation() {
  DoConcatenation();
  // Now the stack is in a state like:
  //   ... ( regexp | regexp | regexp
  // Collapse the alternations of regular expressions into one Alternation.

  vector<Regexp*>::iterator it;
  vector<Regexp*>::iterator begin = stack()->begin();
  vector<Regexp*>::iterator end = stack()->end();
  vector<Regexp*>::iterator last = end - 1;

  // Avoid trivial alternations of zero or one element.
  // TODO(rames): When tracking submatches we need to handle parenthesis around
  // one element.
  if (FLAG_use_parser_opt &&
      ((*last)->IsLeftParenthesis() ||
       ((last - 1) >= begin && (*(last - 1))->IsLeftParenthesis()) ||
       (begin == last))) {
    return;
  }

  Alternation* alt = new Alternation();

  // TODO(rames): Try to find a common substring for alternations of
  // multiplechars?
  // Go down the stack until we find a a left parenthesis or the beginning of
  // the stack.
  vector<Regexp*>::iterator first;
  for (it = last;
       it >= begin && !(*it)->IsLeftParenthesis(); it--) {
    if (!(*it)->IsMarker())
      alt->sub_regexps()->push_back(*it);
  }
  first = (it < begin) ? begin : it + 1;
  stack()->erase(first, end);
  PushRegexp(alt);
}


void Parser::PushAsterisk() {
  // We don't need to update the regexp max length, as it is the same as the max
  // length for the sub regexp.
  PushRegexp(new Repetition(PopRegexp(), 0, kMaxUInt));
}


void Parser::PushPlus() {
  // We don't need to update the regexp max length, as it is the same as the max
  // length for the sub regexp.
  PushRegexp(new Repetition(PopRegexp(), 1, kMaxUInt));
}


void Parser::PushQuestionMark() {
  // We don't need to update the regexp max length, as it is the same as the max
  // length for the sub regexp.
  PushRegexp(new Repetition(PopRegexp(), 0, 1));
}


void Parser::DoFinish() {
  DoAlternation();
  // The stack should now only contain the root regexp.
  if (stack()->size() > 1) {
    // Count the number of open left-parenthesis.
    vector<Regexp*>::iterator it;
    unsigned parenthesis_count = 0;
    for (it = stack()->begin(); it < stack()->end(); it++) {
      parenthesis_count += (*it)->IsLeftParenthesis();
    }
    ASSERT(parenthesis_count);
    ParseError(regexp_string_ + index_,
               "Missing %d right-parenthis ')'.\n",
               parenthesis_count);
  }
}


Status Parser::ParseError(const char* pos, const char *format, ...) {
  unsigned index = pos - regexp_string_;
  int written = snprintf(rejit_status_string, STATUS_STRING_SIZE,
                         "Error parsing at index %d\n%s\n%s^ \n",
                         index, regexp_string_, string(index, ' ').c_str());
  --written;  // Discard the terminating character.
  va_list argptr;
  va_start(argptr, format);
  vsnprintf(rejit_status_string + written, STATUS_STRING_SIZE - written,
            format, argptr);
  va_end(argptr);
  status_ = ParserError;
  return status_;
}


Status Parser::Unexpected(const char* pos) {
  return ParseError(pos, "unexpected character %c\n", *pos);
}


Status Parser::Expected(const char* pos, const char* expected) {
  return ParseError(pos, "expected: %s\n", expected);
}


Status Parser::Expect(const char* c, const char *expected) {
  unsigned i = 0;
  while (*(expected + i)) {
    if (*(c + i) != *(expected + i)) {
      return Expected((c + i), (expected + i));
    }
    i++;
  }
  return RejitSuccess;
}

} }  // namespace rejit::internal
