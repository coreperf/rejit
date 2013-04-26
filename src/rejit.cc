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

#include <iostream>

#include "rejit.h"
#include "checks.h"
#include "parser.h"
#include "codegen.h"

#include "macro-assembler.h"

using namespace rejit::internal;  // NOLINT
using namespace std;              // NOLINT

namespace rejit {

#define __ masm->


bool MatchFull(const char* regexp, const string& text) {
  return MatchFull(regexp, text.c_str(), text.size());
}


bool MatchFull(const char* regexp, const char* text, size_t text_size) {
  Regej re(regexp);
  return re.MatchFull(text, text_size);
}


bool MatchAnywhere(const char* regexp, const string& text) {
  return MatchAnywhere(regexp, text.c_str(), text.size());
}


bool MatchAnywhere(const char* regexp, const char* text, size_t text_size) {
  Regej re(regexp);
  return re.MatchAnywhere(text, text_size);
}


bool MatchFirst(const char* regexp, const string& text, Match* match) {
  return MatchFirst(regexp, text.c_str(), text.size(), match);
}


bool MatchFirst(const char* regexp, const char* text, size_t text_size,
                Match* match) {
  Regej re(regexp);
  return re.MatchFirst(text, text_size, match);
}


size_t MatchAll(const char* regexp, const string& text,
                std::vector<struct Match>* matches) {
  return MatchAll(regexp, text.c_str(), text.size(), matches);
}


size_t MatchAll(const char* regexp, const char* text, size_t text_size,
                std::vector<struct Match>* matches) {
  Regej re(regexp);
  return re.MatchAll(text, text_size, matches);
}


size_t MatchAllCount(const char* regexp, const string& text) {
  return MatchAllCount(regexp, text.c_str(), text.size());
}


size_t MatchAllCount(const char* regexp, const char* text, size_t text_size) {
  Regej re(regexp);
  return re.MatchAllCount(text, text_size);
}


void Replace(Match to_replace, string& text, const string& with) {
  vector<Match> matches;
  matches.push_back(to_replace);
  return Replace(&matches, text, with);
}


void Replace(vector<Match>* to_replace, string& text, const string& with) {
  vector<Match>::iterator it;
  string res;
  // The size of the original string is a good estimate for the size of the
  // replaced string. To avoid memcpies when the size is growing we reserve
  // slightly more.
  res.reserve(text.size() + text.size() / 16);
  const char* ptext = text.c_str();
  for (it = to_replace->begin(); it < to_replace->end(); it++) {
    res.append(ptext, (*it).begin - ptext);
    ptext = (*it).end;
    res.append(with);
  }
  res.append(ptext, text.c_str() + text.size() - ptext);
  text.assign(res);
}


bool ReplaceFirst(const char* regexp, string& text, const string& with) {
  Regej re(regexp);
  return re.ReplaceFirst(text, with);
}


size_t ReplaceAll(const char* regexp, string& text, const string& with) {
  Regej re(regexp);
  return re.ReplaceAll(text, with);
}


Regej::Regej(const char* regexp) :
  regexp_(regexp),
  rinfo_(new RegexpInfo()) {}


Regej::Regej(const string& regexp) :
  regexp_(regexp.c_str()),
  rinfo_(new RegexpInfo()) {}


Regej::~Regej() {
  delete rinfo_;
}


bool Regej::MatchFull(const string& text) {
  return MatchFull(text.c_str(), text.size());
}


bool Regej::MatchFull(const char* text, size_t text_size) {
  if (!rinfo_->match_full_) {
    if (!Compile(kMatchFull)) return false;
  }
  return rinfo_->match_full_(text, text_size);
}


bool Regej::MatchAnywhere(const string& text) {
  return MatchAnywhere(text.c_str(), text.size());
}


bool Regej::MatchAnywhere(const char* text, size_t text_size) {
  if (!rinfo_->match_anywhere_) {
    if (!Compile(kMatchAnywhere)) return false;
  }
  return rinfo_->match_anywhere_(text, text_size);
}


bool Regej::MatchFirst(const string& text, Match* match) {
  return MatchFirst(text.c_str(), text.size(), match);
}


bool Regej::MatchFirst(const char* text, size_t text_size, Match* match) {
  if (!rinfo_->match_first_) {
    if (!Compile(kMatchFirst)) return false;
  }
  return rinfo_->match_first_(text, text_size, match);
}


size_t Regej::MatchAll(const string& text, vector<Match>* matches) {
  return MatchAll(text.c_str(), text.size(), matches);
}


size_t Regej::MatchAll(const char* text, size_t text_size, vector<Match>* matches) {
  if (!rinfo_->match_all_) {
    if (!Compile(kMatchAll)) return 0;
  }
  rinfo_->match_all_(text, text_size, matches);
  return matches->size();
}


size_t Regej::MatchAllCount(const string& text) {
  return MatchAllCount(text.c_str(), text.size());
}


size_t Regej::MatchAllCount(const char* text, size_t text_size) {
  // TODO: Test if using a separate function not registering matches in a vector
  // is faster.
  vector<Match> matches;
  return MatchAll(text, text_size, &matches);
}


bool Regej::ReplaceFirst(string& text, const string& with) {
  Match match;
  bool match_found = MatchFirst(text, &match);
  if (match_found) {
    Replace(match, text, with);
  }
  return match_found;
}


size_t Regej::ReplaceAll(string& text, const string& with) {
  vector<Match> matches;
  MatchAll(text, &matches);
  Replace(&matches, text, with);
  return matches.size();
}


bool Regej::Compile(MatchType match_type) {
  Parser parser;
  parser.ParseERE(rinfo_, regexp_);

  Codegen codegen;
  VirtualMemory* vmem = codegen.Compile(rinfo_, match_type);

  switch (match_type) {
    case kMatchFull:
      rinfo_->vmem_match_full_ = vmem;
      rinfo_->match_full_ =
        FUNCTION_CAST<MatchFullFunc>(Address(vmem->address()));
      break;

    case kMatchAnywhere:
      rinfo_->vmem_match_anywhere_ = vmem;
      rinfo_->match_anywhere_ =
        FUNCTION_CAST<MatchAnywhereFunc>(Address(vmem->address()));
      break;

    case kMatchFirst:
      rinfo_->vmem_match_first_ = vmem;
      rinfo_->match_first_ =
        FUNCTION_CAST<MatchFirstFunc>(Address(vmem->address()));
      break;

    case kMatchAll:
      rinfo_->vmem_match_all_ = vmem;
      rinfo_->match_all_ =
        FUNCTION_CAST<MatchAllFunc>(Address(vmem->address()));
      break;

    default:
      UNREACHABLE();
  }

  return vmem != NULL;
}


}  // namespace rejit
