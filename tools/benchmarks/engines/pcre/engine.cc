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

#include "pcrecpp.h"
#include "../bench_engine.h"

using namespace std;

// Argp configuration ----------------------------------------------------------
const char *argp_program_version = "pcre benchmark engine";


// Benchmark function ----------------------------------------------------------

void pcre_compilation_error() {
  printf("PCRE compilation error.");
  exit(1);
}


int pcre_match_all(pcrecpp::RE *re, string *text) {
  pcrecpp::StringPiece input(*text);
  string var;
  int value;
  while (re->FindAndConsume(&input, &var, &value)) {}
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
    int rc = 0;
    bench_res res;
    struct timeval t0, t1, t2;
    size_t size = res.text_size = *rit;
    text.resize(size);

    if (arguments.run_worst_case) { // Measure worst case speed.
      gettimeofday(&t0, NULL);
      for (unsigned i = 0; i < arguments.iterations; i++) {
        pcrecpp::RE re(regexp);
        rc = pcre_match_all(&re, &text);
        if (rc)
          break;
      }
      gettimeofday(&t1, NULL);

      res.worse = rc != 0 ? 0 : speed(&t0, &t1, size, arguments.iterations);
    }

    { // Compute best and amortised speeds.
      pcrecpp::RE* re;

      gettimeofday(&t0, NULL);

      re = new pcrecpp::RE(regexp);
      if (!re)
        pcre_compilation_error();

      gettimeofday(&t1, NULL);

      for (unsigned i = 0; i < arguments.iterations; i++) {
        rc = pcre_match_all(re, &text);
        if (rc)
          break;
      }

      gettimeofday(&t2, NULL);

      delete re;

      if (rc != 0) {
        res.amortised = 0;
        res.best = 0;
      } else {
        res.amortised = speed(&t0, &t2, size, arguments.iterations);
        res.best = speed(&t1, &t2, size, arguments.iterations);
      }

      results.push_back(res);
    }
  }

  reverse(results.begin(), results.end());
  print_results(&results, arguments.run_worst_case);

  return 0;
}

