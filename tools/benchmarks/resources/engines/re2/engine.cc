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

#include "hg.re2/re2/re2.h"
#include "../utils.h"

using namespace std;

void error(const char* message) {
  printf("ERROR: %s\nExiting.", message);
  exit(1);
}

// Argp configuration ----------------------------------------------------------
const char *argp_program_version = "re2 benchmark engine 0.1";
const char *argp_program_bug_address = "<alexandre@uop.re>";

// Copied from rejit.h
enum MatchType {
  kMatchFull,
  kMatchAnywhere,
  kMatchFirst,
  kMatchAll,
  kNMatchTypes
};

/* This structure is used by main to communicate with parse_opt. */
struct arguments {
  char       *args[1];  // The regexp.
  char       *file;
  size_t     size;
  unsigned   iterations;
  char       low_char;
  char       high_char;
  MatchType  match_type;
};

/*
   OPTIONS.  Field 1 in ARGP.
   Order of fields: {NAME, KEY, ARG, FLAGS, DOC}.
*/
static struct argp_option options[] =
{
  {"file"       , 'f' , ""     , OPTION_ARG_OPTIONAL , "Source file. If none provided, use a randomly generated characters."}, 
  {"size"       , 's' , "1024" , OPTION_ARG_OPTIONAL , "Size of the text to match."},
  {"iterations" , 'i' , "1000" , OPTION_ARG_OPTIONAL , "Number of iterations to run."},
  {"low_char"   , 'l' , "0"    , OPTION_ARG_OPTIONAL , "When the match source is random text, the low character of the range of characters composing the matched text."}, 
  {"high_char"  , 'h' , "z"    , OPTION_ARG_OPTIONAL , "When the match source is random text, the high character of the range of characters composing the matched text."}, 
  {"match_type" , 'm' , "all"  , OPTION_ARG_OPTIONAL , "Type of matching to perform. [all, first]."}, 
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
"Benchmark re2 regular expression engine.\n"
"--- NOTE ---------------------------------\n"
"This utility benchmarks the speed to find the *FIRST* match of the provided "
"regular expression. For coherent results ensure that your regexp does NOT "
"match.\n"
"Why so? Because this is currently what is needed for the automated bencharks, "
"which this utility is designed for.\n"
"Improving this tool to benchmark MatchAll is on my TODO list."
"\n------------------------------------------\n" ;
static struct argp argp = {options, parse_opt, args_doc, doc};


// Benchmark function ----------------------------------------------------------
// We output 3 different speeds, in order:
//  - worst case speed (regexp compilation for each run)
//  - amortised speed (one compilation for 'times' run)
//  - best case speed (compilation time ignored)
int main(int argc, char *argv[]) {

  // Arguments parsing -------------------------------------
  struct arguments arguments;
  arguments.file       = NULL;
  arguments.size       = 1024;
  arguments.iterations = 1000;
  arguments.low_char   = 'a';
  arguments.high_char  = 'z';
  arguments.match_type = kMatchAll;

  argp_parse(&argp, argc, argv, 0, 0, &arguments);

  char *regexp = arguments.args[0];

  // Check the arguments provided.
  if (regexp[0] == 0) {
    error("Cannot test an empty regular expression.");
  }

  // Prepare text to match ---------------------------------

  // The string which will be searched for the regular expression.
  string text;

  // Initialize the text.
  int fd = 0;
  if (arguments.file) {
    // Get the text in the file.
    struct stat file_stats;
    fd = open(arguments.file, O_RDONLY);
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
    gettimeofday(&t0, NULL);
    if (arguments.match_type == kMatchAll) {
      re2::StringPiece piece = text;
      for (unsigned i = 0; i < arguments.iterations; i++) {
        RE2 pattern(regexp);
        while (RE2::FindAndConsume(&piece, pattern)) {}
      }
    } else {
      for (unsigned i = 0; i < arguments.iterations; i++) {
        RE2 pattern(regexp);
        RE2::PartialMatch(text, pattern);
      }
    }
    gettimeofday(&t2, NULL);

    print_speed(t2.tv_sec - t0.tv_sec,
                t2.tv_usec - t0.tv_usec,
                arguments.size, arguments.iterations);
  }

  { // Compute best and amortised speeds.
    gettimeofday(&t0, NULL);
    RE2 pattern(regexp);

    gettimeofday(&t1, NULL);

    if (arguments.match_type == kMatchAll) {
      re2::StringPiece piece = text;
      for (unsigned i = 0; i < arguments.iterations; i++) {
        while (RE2::FindAndConsume(&piece, pattern)) {}
      }
    } else {
      for (unsigned i = 0; i < arguments.iterations; i++) {
        RE2::PartialMatch(text, pattern);
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

