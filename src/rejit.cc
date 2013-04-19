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
  Regej re(regexp);
  return re.MatchFull(text);
}


bool MatchAnywhere(const char* regexp, const string& text) {
  Regej re(regexp);
  return re.MatchAnywhere(text);
}


bool MatchFirst(const char* regexp, const string& text, Match* match) {
  Regej re(regexp);
  return re.MatchFirst(text, match);
}


size_t MatchAll(const char* regexp, const string& text,
                std::vector<struct Match>* matches) {
  Regej re(regexp);
  return re.MatchAll(text, matches);
}


size_t MatchAllCount(const char* regexp, const string& text) {
  Regej re(regexp);
  return re.MatchAllCount(text);
}


void Replace(Match to_replace, string& text, const string& with) {
  vector<Match> matches;
  matches.push_back(to_replace);
  return Replace(&matches, text, with);
}


void Replace(vector<Match>* to_replace, string& text, const string& with) {
  vector<Match>::iterator it;
  string res;
  // TODO: The size of the original string is a good estimate to reserve space
  // and avoid useless memcpy. Reserving more space would allow to gain
  // significant performance when increasing the size of the result string.
  res.reserve(text.size());
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
  if (!rinfo_->match_full_) {
    if (!Compile(kMatchFull)) return false;
  }
  return rinfo_->match_full_(text.c_str(), text.size());
}


bool Regej::MatchAnywhere(const string& text) {
  if (!rinfo_->match_anywhere_) {
    if (!Compile(kMatchAnywhere)) return false;
  }
  return rinfo_->match_anywhere_(text.c_str(), text.size());
}


bool Regej::MatchFirst(const string& text, Match* match) {
  if (!rinfo_->match_first_) {
    if (!Compile(kMatchFirst)) return false;
  }
  return rinfo_->match_first_(text.c_str(), text.size(), match);
}


size_t Regej::MatchAll(const string& text, vector<Match>* matches) {
  if (!rinfo_->match_all_) {
    if (!Compile(kMatchAll)) return 0;
  }
  rinfo_->match_all_(text.c_str(), text.size(), matches);
  return matches->size();
}


size_t Regej::MatchAllCount(const string& text) {
  // TODO: Test if using a separate function not registering matches in a vector
  // is faster.
  vector<Match> matches;
  return MatchAll(text, &matches);
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
