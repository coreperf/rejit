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

#include <iostream>
#include <argp.h>
#include <string.h>

#include "rejit.h"
#include "checks.h"
#include "flags.h"

using namespace std;


// Start the enum from the latest argp key used.
enum rejit_flags_option_keys {
  // Hope it does not collide with other keys.
  base_rejit_flag_key = 0x7BAD,
#define ENUM_KEYS(flag_name, r, d) flag_name##_key,
  REJIT_FLAGS_LIST(ENUM_KEYS)
#undef ENUM_KEYS
};


struct argp_option options[] =
{
  {"line"       , 'l' , "0", OPTION_ARG_OPTIONAL ,
    "Only run the tests from the specified line. (Or 0 to run all tests.)"},
  {"test-id"    , 't' , "0", OPTION_ARG_OPTIONAL ,
    "Only run the test with the specified id. (Or 0 to run all tests.)"},
  {"break_on_fail", 'b' , NULL, OPTION_ARG_OPTIONAL ,
    "Break when a test fails."},
  {"verbose", 'v' , NULL, OPTION_ARG_OPTIONAL ,
    "Print the line and test-id of the tests run."},
  // Convenient access to rejit flags.
#define FLAG_OPTION(flag_name, r, d) \
  {#flag_name , flag_name##_key , FLAG_##flag_name ? "1" : "0"   , OPTION_ARG_OPTIONAL , "0 to disable, 1 to enable."},
  REJIT_FLAGS_LIST(FLAG_OPTION)
#undef FLAG_OPTION
  {0}
};

char doc[] =
"\n"
"Benchmark test program.\n"
"Tests a range of regular expressions and outputs test results.\n";

char args_doc[] = "";

const char *argp_program_bug_address = "<alexandre@coreperf.com>";

struct arguments {
  unsigned line;
  unsigned test_id;
  bool break_on_fail;
  bool verbose;
};
struct arguments arguments;

error_t parse_opt(int key, char *arg, struct argp_state *state) {
  struct arguments *arguments = reinterpret_cast<struct arguments*>(state->input);
  switch (key) {
    case 'b':
      arguments->break_on_fail = true;
      break;
    case 'l':
      if (arg) {
        arguments->line = stol(arg);
      }
      break;
    case 't':
      if (arg) {
        arguments->test_id = stol(arg);
      }
      break;
    case 'v':
      arguments->verbose = true;
      break;
#define FLAG_CASE(flag_name, r, d)                                             \
    case flag_name##_key: {                                                    \
      if (arg) {                                                               \
        unsigned v = stol(arg);                                                \
        assert(v == 0 || v == 1);                                              \
        FLAG_##flag_name = v;                                                  \
      }                                                                        \
      break;                                                                   \
    }
    REJIT_FLAGS_LIST(FLAG_CASE)
#undef FLAG_CASE

    case ARGP_KEY_ARG:
      argp_usage(state);
      break;
    case ARGP_KEY_END:
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

struct argp argp = {options, parse_opt, args_doc, doc};


namespace rejit {

#define x10(s) s s s s s s s s s s
#define x50(s) x10(s) x10(s) x10(s) x10(s) x10(s)
#define x100(s) x50(s) x50(s)


static bool ShouldTest(struct arguments *arguments, int line, int test_id) {
  return (arguments->test_id == test_id) ||
         ((arguments->test_id == 0) &&
          (arguments->line == 0 || arguments->line == line));
}


static void PrintTest(struct arguments *arguments, int line, int test_id) {
  if (arguments->verbose) {
    cout << "Running test line " << line << " test_id " << test_id << endl;
  }
}


static int test_id = 0;
// Every call to this function increments the global counter `test_id`, even
// when the test is skipped.
typedef int TestStatus;
enum {
  TEST_SKIPPED = 0,
  TEST_PASSED = 1,
  TEST_FAILED = -1
};
static TestStatus Test(MatchType match_type,
                       const char* regexp,
                       const string& text,
                       unsigned expected,
                       unsigned line = 0,
                       int expected_start = -1,
                       int expected_end = -1);

static TestStatus TestFull(const char* regexp, const string& text,
                           bool expected,
                           int line = 0);

static int TestMultiple(const char* regexp, const string& text,
                        unsigned expected,
                        unsigned line = 0,
                        int expected_start = -1, int expected_end = -1,
                        bool unbound = false);


int RunTest(struct arguments *arguments) {
  assert(FLAG_benchtest);
  TestStatus local_rc;
  int count_pass = 0;
  int count_fail = 0;

#define UPDATE_RESULTS(local_rc)                                               \
  count_pass += (local_rc == TEST_PASSED);                                     \
  count_fail += (local_rc == TEST_FAILED)                                      \

#define TEST(match_type, expected, regexp, text)                               \
  local_rc = Test(match_type, regexp, string(text), expected, __LINE__);       \
  UPDATE_RESULTS(local_rc)

#define TEST_Full(expected, re, text)                                          \
  local_rc = TestFull(re, string(text), expected, __LINE__);                   \
  UPDATE_RESULTS(local_rc)

#define TEST_Multiple(expected, re, text, start, end)                          \
  local_rc = TestMultiple(re, string(text), expected, __LINE__, start, end);   \
  UPDATE_RESULTS(local_rc)

#define TEST_Multiple_unbound(expected, re, text, start, end)                  \
  local_rc = TestMultiple(                                                     \
      re, string(text), expected, __LINE__, start, end, true);                 \
  UPDATE_RESULTS(local_rc)

  // Test the test routines.
  TEST_Full(1, "x", "x");
  TEST_Full(0, "x", "y");
  TEST_Multiple(1, "x", "x", 0, 1);
  TEST_Multiple(0, "x", "y", 0, 0);
  TEST_Multiple(5, "x", "xxxxx", 0, 1);
  TEST_Multiple(5, "x", "xxxxx", 0, 1);

  // Simple characters.
  TEST_Full(1, "0123456789", "0123456789");
  TEST_Full(0, "0123456789", "0123456789abcd");
  TEST_Multiple_unbound(1, "0123456789", "0123456789",     0, 10);
  TEST_Multiple_unbound(1, "0123456789", "ab0123456789cd", 2, 12);

  // More characters than the maximum number of ring times.
  TEST_Full(1, x10("0123456789"), x10("0123456789"));
  TEST_Full(0, x10("0123456789"), x10("0123456789") "X");
  TEST_Full(0, x10("0123456789"), "X" x10("0123456789"));
  TEST_Full(1, x100("0123456789"), x100("0123456789"));
  TEST_Full(0, x100("0123456789"), x100("0123456789") "X");
  TEST_Full(0, x100("0123456789"), "X" x100("0123456789"));

  // Period.
  TEST_Full(1, "01234.6789", "0123456789");
  TEST_Full(0, "012345678.", "0123456789abcd");
  TEST_Multiple_unbound(1, ".123456789", "0123456789", 0, 10);
  TEST_Multiple_unbound(1, "012345678.", "ab0123456789cd", 2, 12);
  TEST_Full(1, "...", "abc");
  TEST_Full(0, ".", "\n");
  TEST_Full(0, ".", "\r");
  TEST_Full(0, "a.b", "a\nb");
  TEST_Full(0, "a.b", "a\rb");
  TEST_Full(0, "...", "01");
  TEST_Full(0, "..", "012");
  TEST_Multiple(0, "...", "01", 0, 0);
  TEST_Multiple(1, "..", "012", 0, 2);
  TEST_Multiple(0, ".", "\n\n\n\r\r\r", 0, 0);
  TEST_Multiple(1, ".", "\n\n\n\r\r\r.", 6, 7);

  // Start and end of line.
  TEST_Full(1, "^" , "");
  TEST_Full(1, "$" , "");
  TEST_Full(1, "^$", "");
  //TEST_Full(0, "$^", ""); // TODO(rames): Interesting! Check the spec.
  TEST_Full(1, "^$\n^$" , "\n");
  TEST_Full(1, "\n^$"   , "\n");
  TEST_Full(1, "^$\n"   , "\n");

  TEST_Full(0, "^", "x");
  TEST_Full(0, "$", "x");
  TEST_Full(0, "^$", "x");
  TEST_Full(1, "^\n", "\n");
  TEST_Full(1, "\n$", "\n");
  TEST_Full(1, "^\n$", "\n");

  TEST_Multiple(1, "^" , "", 0, 0);
  TEST_Multiple(1, "$" , "", 0, 0);
  TEST_Multiple(1, "^$", "", 0, 0);
  TEST_Multiple(1, "^", "xxx", 0, 0);
  TEST_Multiple(1, "$", "xxx", 3, 3);
  TEST_Multiple(0, "^$", "x\nx", 0, 0);
  TEST_Multiple(0, "$^", "x\nx", 0, 0);
  TEST_Multiple(1, "$\n^", "x\nx", 1, 2);
  TEST_Multiple(1, "^x", "012\nx___", 4, 5);
  TEST_Multiple(1, "x$", "012x\n___", 3, 4);
  TEST_Multiple(0, "^x", "012\n___", 0, 0);
  TEST_Multiple(0, "x$", "012\n___", 0, 0);
  TEST_Multiple_unbound(1, "^xxx", "\nxxx_____________", 1, 4);

  TEST_Multiple(1, "^", "__", 0, 0);
  TEST_Multiple(2, "^", "\n", 0, 0);
  TEST_Multiple(3, "^", "\n\n", 0, 0);
  TEST_Multiple(4, "^", "\n\n\n", 0, 0);

  TEST_Multiple(1, "$", "__", 2, 2);
  TEST_Multiple(2, "$", "\n", 0, 0);
  TEST_Multiple(3, "$", "\n\n", 0, 0);
  TEST_Multiple(4, "$", "\n\n\n", 0, 0);


  // TODO: Results here are debatable. It seems this matches what vim gives.
  // Check the spec.
  TEST(kMatchAll, 6, "(^|$|[x])", "_xxx_x_");
  TEST(kMatchAll, 5, "(^|$|[x])", "xxx_x_");
  TEST(kMatchAll, 5, "(^|$|[x])", "_xxx_x");
  TEST(kMatchAll, 4, "(^|$|[x])", "xxx_x");

  TEST(kMatchAll, 1, "(^|\n)", "\n");
  TEST(kMatchAll, 1, "($|x)", "x");

  // Alternation.
  TEST_Full(1, "0123|abcd|efgh", "abcd");
  TEST_Full(1, "0123|abcd|efgh", "efgh");
  TEST_Full(0, "0123|abcd|efgh", "_efgh___");
  TEST_Multiple_unbound(1, "0123|abcd|efgh", "_abcd___", 1, 5);
  TEST_Multiple_unbound(0, "0123|abcd|efgh", "_efgX___", 0, 0);
  TEST_Multiple_unbound(1, "(0123|abcd)|efgh", "abcd", 0, 4);
  TEST_Multiple_unbound(1, "0000|1111|2222|3333|4444|5555|6666|7777|8888|9999", "_8888_", 1, 5);
  TEST_Multiple_unbound(0, "0000|1111|2222|3333|4444|5555|6666|7777|8888|9999", "_8__8_", 0, 0);

  TEST_Full(1, "..(abcX|abcd)..", "..abcd..");
  TEST_Full(1, "..(abcd|abcX)..", "..abcd..");

  // Alternations and ERE.
  TEST_Full(1, ")", ")");
  TEST_Multiple_unbound(1, ")", "012)___", 3, 4);

  // Repetition.
  TEST_Full(0, "x{3,5}", "x");
  TEST_Full(0, "x{3,5}", "xx");
  TEST_Full(1, "x{3,5}", "xxx");
  TEST_Full(1, "x{3,5}", "xxxx");
  TEST_Full(1, "x{3,5}", "xxxxx");
  TEST_Full(0, "x{3,5}", "xxxxxx");
  TEST_Full(0, "x{3,5}", "xxxxxxxxxxxxx");

  TEST_Full(0, "(ab.){3,5}", "ab.");
  TEST_Full(0, "(ab.){3,5}", "ab.ab.");
  TEST_Full(1, "(ab.){3,5}", "ab.ab.ab.");
  TEST_Full(1, "(ab.){3,5}", "ab.ab.ab.ab.");
  TEST_Full(1, "(ab.){3,5}", "ab.ab.ab.ab.ab.");
  TEST_Full(0, "(ab.){3,5}", "ab.ab.ab.ab.ab.ab.");
  TEST_Full(0, "(ab.){3,5}", "ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.");

  TEST_Full(1, "x{,5}", "");
  TEST_Full(1, "x{,5}", "xxx");
  TEST_Full(1, "x{,5}", "xxxxx");
  TEST_Full(0, "x{,5}", "xxxxxx");
  TEST_Full(0, "x{,5}", "xxxxxxxxxxxx");

  TEST_Full(1, "(ab.){,5}", "");
  TEST_Full(1, "(ab.){,5}", "ab.ab.ab.");
  TEST_Full(1, "(ab.){,5}", "ab.ab.ab.ab.ab.");
  TEST_Full(0, "(ab.){,5}", "ab.ab.ab.ab.ab.ab.");
  TEST_Full(0, "(ab.){,5}", "ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.");

  TEST_Full(0, "x{3,}", "");
  TEST_Full(0, "x{3,}", "xx");
  TEST_Full(1, "x{3,}", "xxx");
  TEST_Full(1, "x{3,}", "xxxxx");
  TEST_Full(1, "x{3,}", "xxxxxxxxxxxx");

  TEST_Full(0, "(ab.){3,}", "");
  TEST_Full(0, "(ab.){3,}", "ab.ab.");
  TEST_Full(1, "(ab.){3,}", "ab.ab.ab.");
  TEST_Full(1, "(ab.){3,}", "ab.ab.ab.ab.ab.");
  TEST_Full(1, "(ab.){3,}", "ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.");

  TEST_Full(0, "(a.){2,3}{2,3}", "a.");
  TEST_Full(0, "(a.){2,3}{2,3}", "a.a.");
  TEST_Full(0, "(a.){2,3}{2,3}", "a.a.a.");
  TEST_Full(1, "(a.){2,3}{2,3}", "a.a.a.a.");
  TEST_Full(1, "(a.){2,3}{2,3}", "a.a.a.a.a.");
  TEST_Full(1, "(a.){2,3}{2,3}", "a.a.a.a.a.a.");
  TEST_Full(1, "(a.){2,3}{2,3}", "a.a.a.a.a.a.a.");
  TEST_Full(1, "(a.){2,3}{2,3}", "a.a.a.a.a.a.a.a.");
  TEST_Full(1, "(a.){2,3}{2,3}", "a.a.a.a.a.a.a.a.a.");
  TEST_Full(0, "(a.){2,3}{2,3}", "a.a.a.a.a.a.a.a.a.a.");

  TEST_Full(1, ".*", "0123456789");
  TEST_Full(1, "0.*9", "0123456789");
  TEST_Full(0, "0.*9", "0123456789abcd");
  TEST_Multiple_unbound(1, "0.*9", "0123456789", 0, 10);
  TEST_Multiple_unbound(1, "0.*9", "____0123456789abcd", 4, 14);

  TEST_Full(1, "a*b*c*", "aaaabccc");
  TEST_Full(1, "a*b*c*", "aaaaccc");
  TEST_Full(1, "a*b*c*", "aaaab");
  TEST_Full(1, "a*b*c*", "bccc");

  TEST_Multiple_unbound(1, "a+", "012aaa_", 3, 6);
  TEST_Multiple_unbound(1, "(a.)+", "012a.a_a-_", 3, 9);
  TEST_Multiple_unbound(2, "(a.)+", "012a.a_a-_a-", 3, 9);

  TEST_Full(1, ".**", "0123456789");
  TEST_Full(1, ".{0,}", "0123456789");
  TEST_Full(1, ".{1,}", "0123456789");
  TEST_Full(1, ".{0,}{0,}", "0123456789");
  TEST_Full(1, ".{0,}{1,}", "0123456789");
  TEST_Full(1, ".{1,}{0,}", "0123456789");
  TEST_Full(1, ".{1,}{1,}", "0123456789");
  TEST_Full(1, ".{0,1}{0,}", "0123456789");
  TEST_Full(1, ".{0,1}{1,}", "0123456789");
  TEST_Full(1, ".x{0,0}.", "..");
  TEST_Full(1, "(.*.*.*)*", "0123456789");
  TEST_Full(1, "(\\d*\\d*\\d*)*", "0123456789");

  // Combinations of alternations and repetitions.
  TEST_Full(1, "(1|22)*", "111122221221221222222");
  TEST_Full(1, "ABCD_(1|22)*_XYZ", "ABCD_111122221221221222222_XYZ");
  TEST_Full(0, "ABCD_(1|22)*_XYZ", "111122221221221222222");
  TEST_Multiple_unbound(1, "(1|22)+", "ABCD_111122221221221222222_XYZ", 5, 26);

  TEST_Full(1, "(0123|abcd)|(efgh)*", "efghefghefgh");
  TEST_Full(1, "(0123|abcd)|(efgh){1,4}", "efghefghefgh");
  TEST_Full(1, "(0123|abcd)|(efgh){0,4}", "efghefghefgh");
  TEST_Full(0, "(0123|abcd)|(efgh){0,2}", "efghefghefgh");

  // Brackets.
  TEST_Full(1, "[0-9]", "0");
  TEST_Full(0, "[^0-9]", "0");
  TEST_Full(1, "[^0-9]", "a");
  TEST_Full(1, "[0-9]abcdefgh", "5abcdefgh");
  TEST_Full(0, "[0-9]abcdefgh", "Xabcdefgh");
  TEST_Full(1, "a[b-x]g", "afg");
  TEST_Full(1, "_[0-9]*_", "__");
  TEST_Full(1, "_[0-9]*_", "_1234567890987654321_");
  TEST_Full(0, "_[0-9]*_", "_123456789_987654321_");
  TEST_Multiple_unbound(1, "[0-9]", "__________0__________", 10, 11);

  TEST_Full(1, "^____$", "____");
  TEST(kMatchFirst, 1, "^____$", "xx\n____");
  TEST(kMatchFirst, 1, "^____$", "____\nxx");
  TEST(kMatchFirst, 1, "^____$", "xx\n____\nxx");

  TEST_Full(1, "(abcd|.)*0123", "x0123");
  TEST(kMatchFirst, 1, "[a]{1,}", "________________a___");
  TEST(kMatchFirst, 0, "[a]{1,}", "________________b___");

  TEST_Full(0, "(123|(efg)*)456", "123efg456");

  TEST(kMatchFirst, 1, "...123456789", "xxx123456789");
  TEST(kMatchFirst, 0, "...123456789", "xx1234567890");

  TEST(kMatchFirst, 1, "^123456789", "123456789");
  TEST(kMatchFirst, 0, "^123456789", "X1234567890");
  TEST(kMatchFirst, 1, "^(aaa|bbb)", "aaa__");
  TEST(kMatchFirst, 1, "^(aaa|bbb)", "____\naaa__");
  TEST(kMatchFirst, 0, "^(aaa|bbb)", "____aba__");

  TEST(kMatchFirst, 1, "123456789$", "123456789");
  TEST(kMatchFirst, 0, "123456789$", "_123456789_");
  TEST(kMatchFirst, 1, "(aaa|bbb)$", "____aaa");
  TEST(kMatchFirst, 1, "(aaa|bbb)$", "____aaa\n");
  TEST(kMatchFirst, 1, "(aaa|bbb)$", "____aaa\n__");
  TEST(kMatchFirst, 0, "(aaa|bbb)$", "____aba__");
  TEST(kMatchFirst, 1, "$(\naaa|\rbbb)", "__\naaa__");

  // Test kMatchAll.
  TEST(kMatchAll, 0, "x", "____________________");
  TEST(kMatchAll, 3, "x", "xxx_________________");
  TEST(kMatchAll, 3, "x", "_________________xxx");
  TEST(kMatchAll, 3, "x", "_x____x____x________");
  TEST(kMatchAll, 4, "x", "_x____xx___x________");
  TEST(kMatchAll, 6, "x", "_x____xx___xxx______");

  TEST(kMatchAll, 0, "ab", "__________________________");
  TEST(kMatchAll, 3, "ab", "ababab____________________");
  TEST(kMatchAll, 3, "ab", "____________________ababab");
  TEST(kMatchAll, 3, "ab", "_ab____ab____ab___________");
  TEST(kMatchAll, 4, "ab", "_ab____abab___ab__________");
  TEST(kMatchAll, 6, "ab", "_ab____abab___ababab______");

  TEST(kMatchAll, 0, "a.", "__________________________");
  TEST(kMatchAll, 3, "a.", "a.a.a._____________________");
  TEST(kMatchAll, 3, "a.", "____________________a.a.a.");
  TEST(kMatchAll, 3, "a.", "_a.____a.____a.___________");
  TEST(kMatchAll, 4, "a.", "_a.____a.a.___a.__________");
  TEST(kMatchAll, 6, "a.", "_a.____a.a.___a.a.a.______");

  TEST(kMatchAll, 4, "x+", "_x__xxx____x____xxxxxx_________");
  TEST(kMatchAll, 4, "(a.)+", "_a.__a.a.a.____a.____a.a.a.a.a.a._________");
  TEST(kMatchAll, 4, "x+", "x__xxx____x____xxxxxx");
  TEST(kMatchAll, 4, "(a.)+", "a.__a.a.a.____a.____a.a.a.a.a.a.");

  // Alternation of fast forward elements.
  TEST_Multiple_unbound(1, "(0|0)", "0", 0, 1);
  TEST_Multiple_unbound(1, "(01|01)", "01", 0, 2);
  TEST_Multiple_unbound(1, "(012|012)", "012", 0, 3);
  TEST_Multiple_unbound(1, "(0123|0123)", "0123", 0, 4);
  TEST_Multiple_unbound(1, "(01234|01234)", "01234", 0, 5);
  TEST_Multiple_unbound(1, "(012345|012345)", "012345", 0, 6);
  TEST_Multiple_unbound(1, "(0123456|0123456)", "0123456", 0, 7);
  TEST_Multiple_unbound(1, "(01234567|01234567)", "01234567", 0, 8);
  TEST_Multiple_unbound(1, "(012345678|012345678)", "012345678", 0, 9);
  TEST_Multiple_unbound(1, "(0123456789|0123456789)", "0123456789", 0, 10);
  TEST_Multiple(2, "(xxx|$)", "___xxx___", 3, 6);
  TEST_Multiple(2, "(xxx|^)", "___xxx___", 0, 0);
  TEST_Multiple_unbound(3, "(xxx|[ab-d])", "___ab___xxx___", 3, 4);
  TEST_Multiple(3, "(xxx|^|$)", "___xxx___", 0, 0);
  TEST(kMatchAll, 3, "(xxx|^|$)", "___xxx___");
  TEST(kMatchAll, 6, "(xxx|^|$)", "___xxx_\n\n__");
  TEST(kMatchAll, 8, "(xxx|^|$|[ab-d])", "___ab___xxx_\n\n__");
  TEST(kMatchAll, 6, "(^|$|[x])", "_xxx_x_");
  TEST(kMatchAll, 5, "(^|$|[x])", "xxx_x_");
  TEST(kMatchAll, 5, "(^|$|[x])", "_xxx_x");
  TEST(kMatchAll, 4, "(^|$|[x])", "xxx_x");
  TEST_Multiple(1, "(.a|a)", "_a_", 0, 2);
  TEST_Multiple(1, "(a|.a)", "_a_", 0, 2);
  TEST_Multiple(1, "(a|.a.)", "_a_", 0, 3);
  TEST_Multiple(1, "(...a|a)", "123a123", 0, 4);
  TEST_Multiple(0, "(....a|a....)", "123a123", 0, 0);
  TEST_Multiple(1, "(.a.|.......a...)", "0123456789a0123456789", 3, 14);
  TEST(kMatchAll, 1, "(.a.|.......a...)", "____a__a__a__________");
  TEST_Multiple(1, "(..ab.|.ab.)", "__ab__", 0, 5);
  TEST_Multiple(1, "(.ab.|..ab.)", "__ab__", 0, 5);
  TEST_Multiple(1, ".(..ab.|.ab.).", "___ab___", 0, 7);
  TEST_Multiple(1, ".(.ab.|..ab.).", "___ab___", 0, 7);
  TEST_Multiple(1, "(..ab.|.ab.X)", "__ab__", 0, 5);
  TEST_Multiple(1, "(.ab.X|..ab.)", "__ab__", 0, 5);
  TEST_Multiple(1, "(..ab.X|.ab.)", "__ab__", 1, 5);
  TEST_Multiple(1, "(.ab.|..ab.X)", "__ab__", 1, 5);
  TEST_Multiple(0, ".(..ab.|.ab.X).", "__ab__", 0, 0);
  TEST_Multiple(0, ".(.ab.X|..ab.).", "__ab__", 0, 0);
  TEST_Multiple(1, ".(..ab.X|.ab.).", "__ab__", 0, 6);
  TEST_Multiple(1, ".(.ab.|..ab.X).", "__ab__", 0, 6);
  TEST_Multiple(0, ".(X.ab.|.ab.X).", "__ab__", 0, 0);
  TEST_Multiple(0, ".(.ab.X|X.ab.).", "__ab__", 0, 0);

  // Special matching patterns.
  TEST_Full(1, "\\d", "5");
  TEST_Full(0, "\\d", "_");
  TEST_Full(0, "\\D", "5");
  TEST_Full(1, "\\D", "_");
  TEST_Full(1, "\\n", "\n");
  TEST_Full(0, "\\n", "\r");
  TEST_Full(1, "\\s", " ");
  TEST_Full(1, "\\s", "\t");
  TEST_Full(0, "\\s", "_");
  TEST_Full(0, "\\s", "_");
  TEST_Full(0, "\\S", " ");
  TEST_Full(0, "\\S", "\t");
  TEST_Full(1, "\\S", "_");
  TEST_Full(1, "\\S", "_");
  TEST_Full(1, "\\t", "\t");
  TEST_Full(0, "\\t", "\n");
  TEST_Full(1, "\\x30", "0");
  TEST_Full(0, "\\x30", "_");

  TEST_Full(1, "(a?)a", "a");
  TEST_Full(1, "(a?){1}a{1}", "a");
  TEST_Full(1, "(a?){2}a{2}", "aa");
  TEST_Full(1, "(a?){5}a{5}", "aaaaa");


  // Control regexps as FF elements just before the end of the regexp.
  TEST_Multiple(1, "x$", "x", 0, 1);
  TEST_Multiple_unbound(1, "x$", "x\n", 0, 1);
  TEST_Multiple(2, "x$", "x\nx", 0, 1);
  TEST_Multiple_unbound(2, "x$", "x\nx\n", 0, 1);

  if (count_fail) {
    printf("FAIL: %d\tpass: %d\t(total: %d)\n", count_fail, count_pass, count_fail + count_pass);
  } else {
    printf("success\n");
  }
  return count_fail;
}


static unsigned DoTest(MatchType match_type,
                       const char* regexp, const string& text,
                       int *found_start = NULL, int *found_end = NULL) {
  unsigned res = 0;
  if (found_start) {
    *found_start = -1;
  }
  if (found_end) {
    *found_end = -1;
  }
  Regej re(regexp);
  vector<Match> matches;
  Match         match;
  switch (match_type) {
    case kMatchFull:
      res = re.MatchFull(text);
      break;
    case kMatchAnywhere:
      res = re.MatchAnywhere(text);
      break;
    case kMatchFirst:
      res = re.MatchFirst(text, &match);
      if (found_start) {
        *found_start = match.begin - text.c_str();
      }
      if (found_end) {
        *found_end = match.end - text.c_str();
      }
      break;
    case kMatchAll:
      re.MatchAll(text, &matches);
      res = matches.size();
      break;

    default:
      UNREACHABLE();
  }
  return res;
}

static TestStatus Test(MatchType match_type,
                       const char* regexp, const string& text,
                       unsigned expected,
                       unsigned line,
                       int expected_start, int expected_end) {
  ++test_id;
  if (!ShouldTest(&arguments, line, test_id)) {
    return TEST_SKIPPED;
  }

  PrintTest(&arguments, line, test_id);


  bool exception = false;
  bool incorrect_limits = false;
  unsigned res = 0;
  int found_start, found_end;

  try {
    res = DoTest(match_type, regexp, text, &found_start, &found_end);
  } catch (int e) {
    exception = true;
  }

  if (match_type == kMatchFirst && expected) {
    incorrect_limits |= expected_start != -1 && found_start != expected_start;
    incorrect_limits |= expected_end != -1 && found_end != expected_end;
  }

  // Correct the expected value depending on the return types for the different
  // match types.
  switch (match_type) {
    case kMatchFull:
    case kMatchAnywhere:
    case kMatchFirst:
      expected = !!expected;
      break;
    case kMatchAll:
      break;
    default:
      break;
  }

  if (exception || res != expected || incorrect_limits) {
    cout << "--- FAILED line " << line << " test_id " << test_id
         << " ------------------------------------------------------" << endl;
    cout << "regexp:\n" << regexp << endl;
    cout << "text:\n" << text << endl;
    cout << "expected: " << expected << "  found: " << res << endl;
    if (expected_start != -1 || expected_end != -1) {
      cout << "      \texpected\tfound" << endl;
    }
    if (expected_start != -1) {
      cout << "start \t" << expected_start << "\t" << found_start << endl;
    }
    if (expected_end != -1) {
      cout << "end   \t" << expected_end << "\t" << found_end << endl;
    }
#define DO_SET_FLAG(flag, ignored_1, ignored_2) SET_FLAG(flag, true);
    REJIT_PRINT_FLAGS_LIST(DO_SET_FLAG)
#undef DO_SET_FLAG
    SET_FLAG(trace_repetitions, true);
    try {
      DoTest(match_type, regexp, text);
    } catch (int e) {}
#define DO_CLEAR_FLAG(flag, ignored_1, ignored_2) SET_FLAG(flag, false);
    REJIT_PRINT_FLAGS_LIST(DO_CLEAR_FLAG)
#undef DO_CLEAR_FLAG
    SET_FLAG(trace_repetitions, false);
    cout << "------------------------------------------------------------------------------------\n\n" << endl;
  }

  int status = (exception || res != expected || incorrect_limits) ? TEST_FAILED
                                                                  : TEST_PASSED;
  if (arguments.break_on_fail) {
    assert(status == TEST_PASSED);
  }
  return status;
}

static TestStatus TestFull(const char* regexp, const string& text,
                           bool expected,
                           int line) {
  TestStatus rc = 0;
  if ((rc |= Test(kMatchFull, regexp, text, expected, line)) == TEST_FAILED)
    return rc;
  if (expected) {
    if ((rc |= Test(kMatchFirst, regexp, text, expected, line)) == TEST_FAILED)
      return rc;
    if ((rc |= Test(kMatchAll, regexp, text, expected, line)) == TEST_FAILED)
      return rc;
  }
  return rc;
}

static int TestMultiple(const char* regexp, const string& text,
                        unsigned expected,
                        unsigned line,
                        int expected_start, int expected_end,
                        bool unbound) {
  TestStatus rc = 0;

  static const int max_alignment = 32;
  static const char fill = ' ';
  int limit = unbound ? max_alignment : 0;

  const string* str = &text;
  string aligned_str;
  for (int i = 0; i <= limit; ++i) {
    if (unbound) {
      aligned_str.clear();
      aligned_str.append(i, fill);
      aligned_str.append(text);
      aligned_str.append(limit - i, fill);
      str = &aligned_str;
    }


    if ((rc |= Test(kMatchFirst, regexp, *str, expected, line)) == TEST_FAILED)
      return rc;
    if ((rc |= Test(kMatchFirst, regexp, *str, expected, line,
                    expected_start + i, expected_end + i)) == TEST_FAILED)
      return rc;
    if ((rc |= Test(kMatchAnywhere, regexp, *str, expected, line)) == TEST_FAILED)
      return rc;
    if ((rc |= Test(kMatchAll, regexp, *str, expected, line)) == TEST_FAILED)
      return rc;
  }

  return rc;
}


}  // namespace rejit


int main(int argc, char *argv[]) {
  memset(&arguments, 0, sizeof(arguments));
  argp_parse(&argp, argc, argv, 0, 0, &arguments);

  return rejit::RunTest(&arguments);
}

#undef TEST
