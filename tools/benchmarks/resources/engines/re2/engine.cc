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
#include <sys/time.h>
#include <string.h>

#include "hg.re2/re2/re2.h"
#include "../bench_engine.h"

using namespace std;

// Argp configuration ----------------------------------------------------------
const char *argp_program_version = "re2 benchmark engine 0.1.2";


// Benchmark function ----------------------------------------------------------
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

  size_t size = arguments.size;
  struct timeval t0, t1, t2;

  { // Measure worst case speed.
    gettimeofday(&t0, NULL);
    for (unsigned i = 0; i < arguments.iterations; i++) {
      re2::StringPiece piece = text;
      RE2 pattern(regexp);
      while (RE2::FindAndConsume(&piece, pattern)) {}
    }
    gettimeofday(&t2, NULL);

    print_speed(t2.tv_sec - t0.tv_sec,
                t2.tv_usec - t0.tv_usec,
                size, arguments.iterations);
  }

  { // Compute best and amortised speeds.
    gettimeofday(&t0, NULL);
    RE2 pattern(regexp);

    gettimeofday(&t1, NULL);

    for (unsigned i = 0; i < arguments.iterations; i++) {
      re2::StringPiece piece = text;
      while (RE2::FindAndConsume(&piece, pattern)) {}
    }

    gettimeofday(&t2, NULL);

    // Amortised speed.
    print_speed(t2.tv_sec - t0.tv_sec,
                t2.tv_usec - t0.tv_usec,
                size, arguments.iterations);
    // Best speed.
    print_speed(t2.tv_sec - t1.tv_sec,
                t2.tv_usec - t1.tv_usec,
                size, arguments.iterations);
  }

  return 0;
}

