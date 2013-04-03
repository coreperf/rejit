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


bool MatchFull(const char* regexp, const char* string) {
  Regej re(regexp);
  return re.MatchFull(string);
}


bool MatchAnywhere(const char* regexp, const char* text) {
  Regej re(regexp);
  return re.MatchAnywhere(text);
}


bool MatchFirst(const char* regexp, const char* text, Match* match) {
  Regej re(regexp);
  return re.MatchFirst(text, match);
}


void MatchAll(const char* regexp, const char* text,
              std::vector<struct Match>* matches) {
  Regej re(regexp);
  return re.MatchAll(text, matches);
}


size_t MatchAllCount(const char* regexp, const char* text) {
  Regej re(regexp);
  return re.MatchAllCount(text);
}


char* Replace(const char* text,
              Match to_replace, const char* with,
              size_t text_size, size_t with_size) {
  vector<Match> matches;
  matches.push_back(to_replace);
  return Replace(text, &matches, with, text_size, with_size);
}


char* Replace(const char* text,
              vector<Match>* to_replace, const char* with,
              size_t text_size, size_t with_size) {
  if (text_size == 0) {
    text_size = strlen(text);
  }
  if (with_size == 0) {
    with_size = strlen(with);
  }
  vector<Match>::iterator it;
  char* res = (char*)malloc(text_size + with_size * to_replace->size());
  char* pres = res;
  const char* ptext = text;
  for (it = to_replace->begin(); it < to_replace->end(); it++) {
    memcpy(pres, ptext, (*it).begin - ptext);
    pres += (*it).begin - ptext;
    ptext = (*it).end;
    memcpy(pres, with, with_size);
    pres += with_size;
  }
  memcpy(pres, ptext, text + text_size - ptext);
  pres += text + text_size - ptext;
  *pres = 0;
  return res;
}


char* ReplaceAll(const char* regexp, const char* text, const char* with,
                 size_t text_size, size_t with_size) {
  Regej re(regexp);
  return re.ReplaceAll(text, with, text_size, with_size);
}


Regej::Regej(const char* regexp) :
  regexp_(regexp),
  rinfo_(new RegexpInfo()) {}


Regej::~Regej() {
  delete rinfo_;
}


bool Regej::MatchFull(const char* s) {
  if (!rinfo_->match_full_) {
    if (!Compile(kMatchFull)) return false;
  }
  return rinfo_->match_full_(s);
}


bool Regej::MatchAnywhere(const char* s) {
  if (!rinfo_->match_anywhere_) {
    if (!Compile(kMatchAnywhere)) return false;
  }
  return rinfo_->match_anywhere_(s);
}


bool Regej::MatchFirst(const char* s, Match* match) {
  if (!rinfo_->match_first_) {
    if (!Compile(kMatchFirst)) return false;
  }
  return rinfo_->match_first_(s, match);
}


void Regej::MatchAll(const char* s, vector<Match>* matches) {
  if (!rinfo_->match_all_) {
    if (!Compile(kMatchAll)) return;
  }
  rinfo_->match_all_(s, matches);
}


size_t Regej::MatchAllCount(const char* text) {
  // TODO: Test if using a separate function not registering matches in a vector
  // is faster.
  vector<Match> matches;
  size_t match_count;
  MatchAll(text, &matches);
  match_count = matches.size();
  return match_count;
}


char* Regej::ReplaceAll(const char* text, const char* with,
                        size_t text_size, size_t with_size) {
  char* res;
  vector<Match> matches;
  MatchAll(text, &matches);
  res = Replace(text, &matches, with, text_size, with_size);
  return res;
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
