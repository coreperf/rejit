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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <string.h>

// Arugments parsing.
#include <argp.h>
// Used to get the file size.
#include <sys/stat.h>

#include "rejit.h"
#include "checks.h"
#include "flags.h"

#include "../utils.h"

using namespace rejit;
using namespace std;

// TODO(rames): Merge the code for rejit and re2 engines.

void error(const char* message) {
  printf("ERROR: %s\nExiting.\n", message);
  exit(1);
}

// Argp configuration ----------------------------------------------------------
const char *argp_program_version = "rejit benchmark engine 0.1";
const char *argp_program_bug_address = "<alexandre@uop.re>";

/* This structure is used by main to communicate with parse_opt. */
struct arguments {
  char      *args[1];  // The regexp.
  char      *file;
  size_t    size;
  unsigned  iterations;
  char      low_char;
  char      high_char;
  MatchType match_type;
  // Rejit flags.
  int  rejit_flags;
};

// Start the enum from the latest argp key used.
enum rejit_flags_option_keys {
  last_argp_key = ARGP_KEY_FINI,
#define ENUM_KEYS(flag_name, r, d) flag_name##_key,
  REJIT_FLAGS_LIST(ENUM_KEYS)
#undef ENUM_KEYS
  first_rejit_flag_key = last_argp_key + 1
};
#define REJIT_FLAG_OFFSET(flag_name) (flag_name##_key - first_rejit_flag_key)

/*
   OPTIONS.  Field 1 in ARGP.
   Order of fields: {NAME, KEY, ARG, FLAGS, DOC}.
*/
static struct argp_option options[] =
{
  {"file"       , 'f' , ""     , OPTION_ARG_OPTIONAL , "Source file. If none provided, use a randomly generated characters."}, 
  {"size"       , 's' , "65536", OPTION_ARG_OPTIONAL , "Size of the text to match."},
  {"iterations" , 'i' , "1000" , OPTION_ARG_OPTIONAL , "Number of iterations to run."},
  {"low_char"   , 'l' , "0"    , OPTION_ARG_OPTIONAL , "When the match source is random text, the low character of the range of characters composing the matched text."}, 
  {"high_char"  , 'h' , "z"    , OPTION_ARG_OPTIONAL , "When the match source is random text, the high character of the range of characters composing the matched text."}, 
  {"match_type" , 'm' , "all"  , OPTION_ARG_OPTIONAL , "Type of matching to perform. [all, first]."}, 
  // Convenient access to rejit flags.
#define FLAG_OPTION(flag_name, r, d) \
  {#flag_name , flag_name##_key , FLAG_##flag_name ? "1" : "0"   , OPTION_ARG_OPTIONAL , "0 to disable, 1 to enable."},
  REJIT_FLAGS_LIST(FLAG_OPTION)
#undef FLAG_OPTION
  {0}
};

static int argtoi(const char *arg) {
  while (*arg == ' ') arg++;
  return atoi(arg);
}

static error_t
parse_opt(int key, char *arg, struct argp_state *state) {
  struct arguments *arguments = reinterpret_cast<struct arguments*>(state->input);
  switch (key) {
    case 'f':
      arguments->file = arg;
      break;
    case 's':
      if (arg)
        arguments->size = argtoi(arg);
      break;
    case 'i':
      if (arg) {
        arguments->iterations = argtoi(arg);
        if (arguments->iterations == 0) {
          error("The number of iterations to run must be greater than 0.");
        }
      }
      break;
    case 'l':
      arguments->low_char = arg[0];
      break;
    case 'h':
      arguments->high_char = arg[0];
      break;
    case 'm':
      if (strcmp(arg, "all") == 0) {
        arguments->match_type = kMatchAll;
      } else if (strcmp(arg, "first") == 0) {
        arguments->match_type = kMatchFirst;
      } else {
        argp_usage(state);
      }
      break;

#define FLAG_CASE(flag_name, r, d)                                             \
    case flag_name##_key: {                                                    \
      if (arg) {                                                               \
        unsigned v;                                                            \
        v = argtoi(arg);                                                       \
        assert(v == 0 || v == 1);                                              \
        arguments->rejit_flags |= v << REJIT_FLAG_OFFSET(flag_name);           \
      }                                                                        \
      break;                                                                   \
    }
    REJIT_FLAGS_LIST(FLAG_CASE)
#undef FLAG_CASE

    case ARGP_KEY_ARG:
      if (state->arg_num >= 1) {
        argp_usage(state);
      }
      arguments->args[state->arg_num] = arg;
      break;
    case ARGP_KEY_END:
      if (state->arg_num < 1) {
        argp_usage(state);
      }
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static char args_doc[] = "regexp";
static char doc[] =
"\n"
"Benchmark rejit regular expression engine.\n"
"\n"
"Output: processing speed in bytes/s (<size of text matched> / <time to match>)\n"
"\t<worst speed> (1 run for 1 compilation)\n"
"\t<amortised speed> (<--iterations=?> runs for 1 compilation)\n"
"\t<best speed> (without considering compilation time)\n"
"When benchmarking using --match_type=first, be careful that your regular "
"expression does not match or you wil end up with surprising performance "
"results!"
"";
static struct argp argp = {options, parse_opt, args_doc, doc};


// Benchmark function ----------------------------------------------------------
// We output 3 different speeds, in order:
//  - worst case speed (regexp compilation for each run)
//  - amortised speed (one compilation for 'times' run)
//  - best case speed (compilation time ignored)
int main(int argc, char *argv[]) {

  // Arguments parsing -------------------------------------
  struct arguments arguments;
  // Set default values.
  arguments.file       = NULL;
  arguments.size       = 65536;
  arguments.iterations = 1000;
  arguments.low_char   = 'a';
  arguments.high_char  = 'z';
  arguments.match_type = kMatchAll;
#define SET_FLAG_DEFAULT(flag_name, r, d)                                      \
  arguments.rejit_flags |= FLAG_##flag_name << REJIT_FLAG_OFFSET(flag_name);
  arguments.rejit_flags = 0;
  REJIT_FLAGS_LIST(SET_FLAG_DEFAULT)
#undef SET_FLAG_DEFAULT

  argp_parse(&argp, argc, argv, 0, 0, &arguments);
  // Set rejit flags according to the arguments.
#define SET_REJIT_FLAG(flag_name, r, d)                                        \
  SET_FLAG(flag_name,                                                          \
           arguments.rejit_flags & (1 << REJIT_FLAG_OFFSET(flag_name)));
REJIT_FLAGS_LIST(SET_REJIT_FLAG)
#undef SET_REJIT_FLAG

  char *regexp = arguments.args[0];
  if (regexp[0] == 0) {
    error("Cannot test an empty regular expression.");
  }

  Regej test_regexp(regexp);
  if (test_regexp.status()) {
    printf("%s\n", rejit_status_string);
    error("Invalid regular expression.\n");
  }

  // Prepare text to match ---------------------------------

  // The string which will be searched for the regular expression.
  string text;

  // Initialize the text.
  if (arguments.file) {
    // Use the content of the specified file to fill text. If the file is
    // smaller than the requested test size, copy it multiple times.
    struct stat file_stats;
    int fd = open(arguments.file, O_RDONLY);
    fstat(fd, &file_stats);
    char* file_text = (char*)mmap(NULL, file_stats.st_size,
                                  PROT_READ, MAP_PRIVATE, fd, 0);
    // Substract one to avoid the terminating character.
    size_t file_text_size = file_stats.st_size - 1;
    // Fill the text to search.
    // If the source file is not big enough loop over it.
    size_t offset = 0;
    size_t copy_size;
    while (offset < arguments.size) {
      copy_size = min(arguments.size - offset, file_text_size);
      text.append(file_text, copy_size);
      offset += copy_size;
    }

    munmap(file_text, file_stats.st_size);

  } else {
    text.resize(arguments.size);
    // Fill the text with random characters.
    for (size_t i = 0; i < arguments.size - 1; i++) {
      text[i] = arguments.low_char +
        (rand() % (arguments.high_char - arguments.low_char));
    }
  }

  // Run ---------------------------------------------------

  struct timeval t0, t1, t2;

  { // Measure worst case speed.
    vector<Match> matches;
    gettimeofday(&t0, NULL);
    for (unsigned i = 0; i < arguments.iterations; i++) {
      Regej re(regexp);
      if (arguments.match_type == kMatchAll) {
        re.MatchAll(text, &matches);
      } else {
        re.MatchFirst(text, NULL);
      }
    }
    gettimeofday(&t2, NULL);

    print_speed(t2.tv_sec - t0.tv_sec,
                t2.tv_usec - t0.tv_usec,
                arguments.size, arguments.iterations);
  }

  { // Compute best and amortised speeds.
    vector<Match> matches;
    gettimeofday(&t0, NULL);
    Regej re(regexp);

    re.Compile(arguments.match_type);

    gettimeofday(&t1, NULL);

    if (arguments.match_type == kMatchAll) {
      for (unsigned i = 0; i < arguments.iterations; i++) {
        re.MatchAll(text, &matches);
      }
    } else {
      for (unsigned i = 0; i < arguments.iterations; i++) {
        re.MatchFirst(text, NULL);
      }
    }

    gettimeofday(&t2, NULL);

    // Amortised speed.
    print_speed(t2.tv_sec - t0.tv_sec,
                t2.tv_usec - t0.tv_usec,
                arguments.size, arguments.iterations);
    // Best speed.
    print_speed(t2.tv_sec - t1.tv_sec,
                t2.tv_usec - t1.tv_usec,
                arguments.size, arguments.iterations);
  }

  return 0;
}

