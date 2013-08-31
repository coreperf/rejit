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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <string.h>
#include <assert.h>

// Arugments parsing.
#include <argp.h>
// Used to get the file size.
#include <sys/stat.h>

#include "../utils.h"
#include "pcre.h"

using namespace std;

#define OVECCOUNT 30    /* pcre doc: should be a multiple of 3 */

void error(const char* message) {
  printf("ERROR: %s\nExiting.", message);
  exit(1);
}

// Argp configuration ----------------------------------------------------------
const char *argp_program_version = "pcre benchmark engine 0.1";
const char *argp_program_bug_address = "<alexandre@coreperf.com>";

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
  {"size"       , 's' , "65536", OPTION_ARG_OPTIONAL , "Size of the text to match."},
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
"\n"
"Benchmark pcre regular expression engine.\n"
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


void pcre_error() {
  printf("Error benchmarking PCRE. Exiting.\n");
  exit(1);
}

void pcre_compilation_error(int erroffset, const char *error) {
  printf("PCRE compilation failed at offset %d: %s\nExiting.",
         erroffset, error);
  exit(1);
}


int pcre_match_one(pcre *re, string *text) {
  int ovector[OVECCOUNT];
  return pcre_exec(re,
                   NULL,
                   text->c_str(),
                   text->size(),
                   0,
                   0,
                   ovector,
                   OVECCOUNT);
}


int pcre_match_all(pcre *re, string *text) {
  // Adaptation of pcredemo.c
  const char *error;
  char *pattern;
  const char *subject;
  unsigned char *name_table;
  unsigned int option_bits;
  int erroffset;
  int crlf_is_newline;
  int namecount;
  int name_entry_size;
  int ovector[OVECCOUNT];
  int subject_length;
  int rc, i;
  int utf8;

  subject = text->c_str();
  subject_length = text->size();

  rc = pcre_exec(
      re,                   /* the compiled pattern */
      NULL,                 /* no extra data - we didn't study the pattern */
      subject,              /* the subject string */
      subject_length,       /* the length of the subject */
      0,                    /* start at offset 0 in the subject */
      0,                    /* default options */
      ovector,              /* output vector for substring information */
      OVECCOUNT);           /* number of elements in the output vector */

  if (rc < 0) {
    switch (rc) {
      case PCRE_ERROR_NOMATCH:
        break;
      default:
        pcre_error();
        break;
    }
    return 1;
  }

  (void)pcre_fullinfo(re, NULL, PCRE_INFO_OPTIONS, &option_bits);
  utf8 = option_bits & PCRE_UTF8;
  option_bits &= PCRE_NEWLINE_CR|PCRE_NEWLINE_LF|PCRE_NEWLINE_CRLF|
    PCRE_NEWLINE_ANY|PCRE_NEWLINE_ANYCRLF;

  /* If no newline options were set, find the default newline convention from the
     build configuration. */

  if (option_bits == 0)
  {
    int d;
    (void)pcre_config(PCRE_CONFIG_NEWLINE, &d);
    /* Note that these values are always the ASCII ones, even in
       EBCDIC environments. CR = 13, NL = 10. */
    option_bits = (d == 13)? PCRE_NEWLINE_CR :
      (d == 10)? PCRE_NEWLINE_LF :
      (d == (13<<8 | 10))? PCRE_NEWLINE_CRLF :
      (d == -2)? PCRE_NEWLINE_ANYCRLF :
      (d == -1)? PCRE_NEWLINE_ANY : 0;
  }

  /* See if CRLF is a valid newline sequence. */

  crlf_is_newline =
    option_bits == PCRE_NEWLINE_ANY ||
    option_bits == PCRE_NEWLINE_CRLF ||
    option_bits == PCRE_NEWLINE_ANYCRLF;

  /* Loop for second and subsequent matches */

  for (;;)
  {
    int options = 0;                 /* Normally no options */
    int start_offset = ovector[1];   /* Start at end of previous match */

    /* If the previous match was for an empty string, we are finished if we are
       at the end of the subject. Otherwise, arrange to run another match at the
       same point to see if a non-empty match can be found. */

    if (ovector[0] == ovector[1])
    {
      if (ovector[0] == subject_length) break;
      options = PCRE_NOTEMPTY_ATSTART | PCRE_ANCHORED;
    }

    /* Run the next matching operation */

    rc = pcre_exec(
        re,                   /* the compiled pattern */
        NULL,                 /* no extra data - we didn't study the pattern */
        subject,              /* the subject string */
        subject_length,       /* the length of the subject */
        start_offset,         /* starting offset in the subject */
        options,              /* options */
        ovector,              /* output vector for substring information */
        OVECCOUNT);           /* number of elements in the output vector */

    /* This time, a result of NOMATCH isn't an error. If the value in "options"
       is zero, it just means we have found all possible matches, so the loop ends.
       Otherwise, it means we have failed to find a non-empty-string match at a
       point where there was a previous empty-string match. In this case, we do what
       Perl does: advance the matching position by one character, and continue. We
       do this by setting the "end of previous match" offset, because that is picked
       up at the top of the loop as the point at which to start again.

       There are two complications: (a) When CRLF is a valid newline sequence, and
       the current position is just before it, advance by an extra byte. (b)
       Otherwise we must ensure that we skip an entire UTF-8 character if we are in
       UTF-8 mode. */

    if (rc == PCRE_ERROR_NOMATCH) {
      if (options == 0) break;                    /* All matches found */
      ovector[1] = start_offset + 1;              /* Advance one byte */
      if (crlf_is_newline &&                      /* If CRLF is newline & */
          start_offset < subject_length - 1 &&    /* we are at CRLF, */
          subject[start_offset] == '\r' &&
          subject[start_offset + 1] == '\n')
        ovector[1] += 1;                          /* Advance by one more. */
      else if (utf8)                              /* Otherwise, ensure we */
      {                                         /* advance a whole UTF-8 */
        while (ovector[1] < subject_length)       /* character. */
        {
          if ((subject[ovector[1]] & 0xc0) != 0x80) break;
          ovector[1] += 1;
        }
      }
      continue;    /* Go round the loop again */
    }

    /* Other matching errors are not recoverable. */

    if (rc < 0) {
      pcre_error();
      return 1;
    }
  }      /* End of loop to find second and subsequent matches */

  return 0;
}


// Benchmark function ----------------------------------------------------------
// We output 3 different speeds, in order:
//  - worst case speed (regexp compilation for each run)
//  - amortised speed (one compilation for 'times' run)
//  - best case speed (compilation time ignored)
int main(int argc, char *argv[]) {

  // Arguments parsing -------------------------------------
  struct arguments arguments;
  arguments.file       = NULL;
  arguments.size       = 65536;
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
      for (unsigned i = 0; i < arguments.iterations; i++) {
        pcre *re;
        const char *error;
        int erroffset;
        re = pcre_compile(regexp,
                          0,
                          &error,
                          &erroffset,
                          NULL);
        if (!re) pcre_compilation_error(erroffset, error);
        pcre_match_all(re, &text);
        pcre_free(re);
      }
    } else {
      for (unsigned i = 0; i < arguments.iterations; i++) {
        pcre *re;
        const char *error;
        int erroffset;
        re = pcre_compile(regexp,
                          0,
                          &error,
                          &erroffset,
                          NULL);
        if (!re) pcre_compilation_error(erroffset, error);
        pcre_match_one(re, &text);
        pcre_free(re);     /* Release memory used for the compiled pattern */
      }
    }
    gettimeofday(&t2, NULL);

    print_speed(t2.tv_sec - t0.tv_sec,
                t2.tv_usec - t0.tv_usec,
                arguments.size, arguments.iterations);
  }

  { // Compute best and amortised speeds.
    pcre *re;
    const char *error;
    int erroffset;

    gettimeofday(&t0, NULL);

    re = pcre_compile(regexp,
                      0,
                      &error,
                      &erroffset,
                      NULL);
    if (!re) pcre_compilation_error(erroffset, error);

    gettimeofday(&t1, NULL);

    if (arguments.match_type == kMatchAll) {
      for (unsigned i = 0; i < arguments.iterations; i++) {
        pcre_match_all(re, &text);
      }
    } else {
      for (unsigned i = 0; i < arguments.iterations; i++) {
        pcre_match_one(re, &text);
      }
    }

    pcre_free(re);

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

