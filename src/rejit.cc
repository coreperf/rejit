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
#include "nb-codegen.h"

#include "macro-assembler.h"

using namespace rejit::internal;  // NOLINT
using namespace std;              // NOLINT

namespace rejit {

#define __ masm->


static void printtype (MatchType match_type) {
  switch (match_type) {
    case kMatchFull:
      cout << "kMatchFull" ;
      break;
    case kMatchAnywhere:
      cout << "kMatchAnywhere" ;
      break;
    case kMatchFirst:
      cout << "kMatchFirst" ;
      break;
    case kMatchAll:
      cout << "kMatchAllë" ;
      break;
    default:
      UNREACHABLE();
  }
}

static bool Test(MatchType match_type, unsigned expected,
                 const char* regexp, const char* string,
                 int line) {
  bool error = false;
  unsigned res;

  //if (match_type == kMatchFirst) {
  //  Test(kMatchAnywhere, expected, regexp, string, line);
  //}

  try {
    Regej re(regexp);
    vector<Match> matches;
    Match         match;
    switch (match_type) {
      case kMatchFull:
        res = re.MatchFull(string);
        break;
      case kMatchAnywhere:
        res = re.MatchAnywhere(string);
        break;
      case kMatchFirst:
        res = re.MatchFirst(string, &match);
        break;
      case kMatchAll:
        re.MatchAll(string, &matches);
        res = matches.size();
        break;

      default:
        UNREACHABLE();
    }
  } catch (int e) {
    error = true;
  }

  if (error || res != expected) {
    cout << "--- FAILED rejit test line " << line
         << " ------------------------------------------------------" << endl;
    cout << "regexp:\n" << regexp << endl;
    cout << "string:\n" << string << endl;
    cout << "expected: " << expected << "  found: " << res << endl;
    SET_FLAG(trace_repetitions, true);
    SET_FLAG(trace_re_tree, true);
    SET_FLAG(trace_re_list, true);
    SET_FLAG(trace_ff_finder, true);
    SET_FLAG(print_state_ring_info, true);
    Regej re(regexp);
    vector<Match> matches;
    Match         match;
    switch (match_type) {
      case kMatchFull:
        res = re.MatchFull(string);
        break;
      case kMatchAnywhere:
        res = re.MatchAnywhere(string);
        break;
      case kMatchFirst:
        res = re.MatchFirst(string, &match);
        break;
      case kMatchAll:
        re.MatchAll(string, &matches);
        res = matches.size();
        break;

      default:
        UNREACHABLE();
    }
    SET_FLAG(trace_repetitions, false);
    SET_FLAG(trace_re_tree, false);
    SET_FLAG(trace_re_list, false);
    SET_FLAG(trace_ff_finder, false);
    SET_FLAG(print_state_ring_info, false);
    cout << "------------------------------------------------------------------------------------\n\n" << endl;
  }
  return res == expected;
}

#define TEST(match_type, expected, regexp, string) \
  Test(match_type, expected, regexp, string, __LINE__);


static bool TestFirst(unsigned expected,
                      const char* regexp,
                      const char* string,
                      unsigned expected_start, 
                      unsigned expected_end, 
                      int line) __attribute__((unused));

#define TEST_First(expected, regexp, string, expected_start, expected_end) \
  TestFirst(expected, regexp, string, expected_start, expected_end, __LINE__);




void RejitTest() {
  std::cout << "RejitTest" << std::endl;

  SET_FLAG(print_state_ring_info, true);
  SET_FLAG(trace_re_tree, true);
  SET_FLAG(trace_re_list, true);
  SET_FLAG(trace_ff_finder, true);

  if (false)
    TEST(kMatchAll, 2, "x", "_x__xx__");

  TEST_First(1, "0123456789", "0123456789",     0, 10);

}

Regej::Regej(const char* regexp) :
  regexp_(regexp),
  rinfo_(new RegexpInfo()) {}


Regej::~Regej() {
  delete rinfo_;
}


bool Regej::Compile(MatchType match_type) {
  Parser parser;
  parser.ParseERE(rinfo_, regexp_);

  NB_Codegen codegen;
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


static bool TestFirst(unsigned expected,
                      const char* regexp,
                      const char* string,
                      unsigned expected_start, 
                      unsigned expected_end, 
                      int line) {
  bool error = false;
  unsigned res, found_start, found_end;

  // Safety check.
  //Test(kMatchAnywhere, expected, regexp, string, line);

  try {
    Regej re(regexp);
    Match match;
    res = re.MatchFirst(string, &match);
    found_start = match.begin - string;
    found_end = match.end - string;
    error = found_end != expected_end || found_start != expected_start;

  } catch (int e) {
    error = true;
  }

  if (error || (res != expected)) {
    cout << "--- FAILED rejit test ";
    printtype(kMatchFirst);
    cout << " line " << line
         << " ------------------------------------------------------" << endl;
    cout << "regexp:\n" << regexp << endl;
    cout << "string:\n" << string << endl;
    cout << "expected: " << expected << "  found: " << res << endl;
    cout << "found    start:end: " << found_start << ":" << found_end << endl;
    cout << "expected start:end: " << expected_start << ":" << expected_end << endl;
    SET_FLAG(trace_repetitions, true);
    SET_FLAG(trace_re_tree, true);
    SET_FLAG(trace_re_list, true);
    SET_FLAG(trace_ff_finder, true);
    SET_FLAG(print_state_ring_info, true);

    Regej re(regexp);
    Match match;
    res = re.MatchFirst(string, &match);

    SET_FLAG(trace_repetitions, false);
    SET_FLAG(trace_re_tree, false);
    SET_FLAG(trace_re_list, false);
    SET_FLAG(trace_ff_finder, false);
    SET_FLAG(print_state_ring_info, false);
    cout << "------------------------------------------------------------------------------------\n\n" << endl;
  }
  return res == expected;
}
#undef DEFINE_MATCH_FUNC


}  // namespace rejit
