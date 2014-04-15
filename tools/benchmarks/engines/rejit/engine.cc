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
#include <string>

#include "../bench_engine.h"

#include "rejit.h"
using namespace rejit;

using namespace std;

// TODO(rames): Merge the code for rejit and re2 engines.

// Argp configuration ----------------------------------------------------------
const char *argp_program_version = "rejit benchmark engine";


// Benchmark function ----------------------------------------------------------
// We output 3 different speeds, in order:
//  - worst case speed (regexp compilation for each run)
//  - amortised speed (one compilation for 'times' run)
//  - best case speed (compilation time ignored)
int main(int argc, char *argv[]) {
  struct arguments arguments;
  handle_arguments(&arguments, &argp, argc, argv);
  char *regexp = arguments.args[0];

  // Set rejit flags according to the arguments.
#define SET_REJIT_FLAG(flag_name, r, d)                                        \
  SET_FLAG(flag_name,                                                          \
           arguments.rejit_flags & (1 << REJIT_FLAG_OFFSET(flag_name)));
REJIT_FLAGS_LIST(SET_REJIT_FLAG)
#undef SET_REJIT_FLAG

  Regej test_regexp(regexp);
  if (test_regexp.status()) {
    printf("%s\n", rejit_status_string);
    error("Invalid regular expression.\n");
  }

  string text;
  prepare_text(&arguments, &text);

  // Run ---------------------------------------------------

  vector<bench_res> results;

  for (unsigned i = 0; i < arguments.sizes.size(); i++) {
    struct timeval t0, t1, t2;
    bench_res res;
    size_t size = res.text_size = arguments.sizes.at(i);

    if (arguments.run_worst_case) { // Measure worst case speed.
      vector<Match> matches;
      gettimeofday(&t0, NULL);
      for (unsigned i = 0; i < arguments.iterations; i++) {
        Regej re(regexp);
        re.MatchAll(text.c_str(), size, &matches);
      }
      gettimeofday(&t1, NULL);

      res.worse = speed(&t0, &t1, size, arguments.iterations);
    }

    { // Compute best and amortised speeds.
      vector<Match> matches;
      gettimeofday(&t0, NULL);
      Regej re(regexp);

      re.Compile(kMatchAll);

      gettimeofday(&t1, NULL);

      for (unsigned i = 0; i < arguments.iterations; i++) {
        re.MatchAll(text.c_str(), size, &matches);
      }

      gettimeofday(&t2, NULL);

      res.amortised = speed(&t0, &t2, size, arguments.iterations);
      res.best = speed(&t1, &t2, size, arguments.iterations);

      results.push_back(res);
    }
  }

  print_results(&results, arguments.run_worst_case);

  return 0;
}

