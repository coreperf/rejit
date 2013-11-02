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
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>

#include "pcre.h"
#include "../bench_engine.h"

using namespace std;

// Argp configuration ----------------------------------------------------------
const char *argp_program_version = "pcre benchmark engine 0.1.2";


// Benchmark function ----------------------------------------------------------

#define OVECCOUNT 30    /* pcre doc: should be a multiple of 3 */

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
  const char *subject;
  unsigned int option_bits;
  int crlf_is_newline;
  int ovector[OVECCOUNT];
  int subject_length;
  int rc;
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


// We output 3 different speeds, in order:
//  - worst case speed (regexp compilation for each run)
//  - amortised speed (one compilation for 'times' run)
//  - best case speed (compilation time ignored)
int main(int argc, char *argv[]) {
  struct arguments arguments;
  handle_arguments(&arguments, &argp, argc, argv);
  char *regexp = arguments.args[0];


  string text;
  prepare_text(&arguments, &text);

  // Run ---------------------------------------------------

  vector<bench_res> results;

  sort(arguments.sizes.begin(), arguments.sizes.end());

  vector<size_t>::reverse_iterator rit;
  for (rit = arguments.sizes.rbegin(); rit < arguments.sizes.rend(); ++rit) {
    bench_res res;
    struct timeval t0, t1, t2;
    size_t size = res.text_size = *rit;
    text.resize(size);

    { // Measure worst case speed.
      gettimeofday(&t0, NULL);
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
      gettimeofday(&t1, NULL);

      res.worse = speed(&t0, &t1, size, arguments.iterations);
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
      if (!re)
        pcre_compilation_error(erroffset, error);

      gettimeofday(&t1, NULL);

      for (unsigned i = 0; i < arguments.iterations; i++) {
        pcre_match_all(re, &text);
      }

      gettimeofday(&t2, NULL);

      res.amortised = speed(&t0, &t2, size, arguments.iterations);
      res.best = speed(&t1, &t2, size, arguments.iterations);

      results.push_back(res);
    }
  }

  reverse(results.begin(), results.end());
  print_results(&results);

  return 0;
}

