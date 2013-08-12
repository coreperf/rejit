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


#include "rejit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <thread>
#include <atomic>
#include <algorithm>

using namespace std;

char const * const dna8mers[] = {
  "agggtaaa|tttaccct",
  "[cgt]gggtaaa|tttaccc[acg]",
  "a[act]ggtaaa|tttacc[agt]t",
  "ag[act]gtaaa|tttac[agt]ct",
  "agg[act]taaa|ttta[agt]cct",
  "aggg[acg]aaa|ttt[cgt]ccct",
  "agggt[cgt]aa|tt[acg]accct",
  "agggta[cgt]a|t[acg]taccct",
  "agggtaa[cgt]|[acg]ttaccct"
};

char const * const iub_codes[] = {
  "B", "(c|g|t)",
  "D", "(a|g|t)",
  "H", "(a|c|t)",
  "K", "(g|t)",
  "M", "(a|c)",
  "N", "(a|c|g|t)",
  "R", "(a|g)",
  "S", "(c|g)",
  "V", "(a|c|g)",
  "W", "(a|t)",
  "Y", "(c|t)"
};

// Use global variables to simplify the implementation of the thread functions.
const unsigned n_mers = sizeof(dna8mers) / sizeof(dna8mers[0]);
const unsigned n_iub_codes = sizeof(iub_codes) / sizeof(iub_codes[0]) / 2;
unsigned counts[n_mers];
vector<rejit::Match> iub_codes_matches[n_iub_codes];
string text;

thread *threads;
atomic_uint processed_mers = ATOMIC_VAR_INIT(0);
atomic_uint processed_iub_codes = ATOMIC_VAR_INIT(0);


void thread_count_mers() {
  unsigned index;
  while ((index = processed_mers++) < n_mers) {
    counts[index] = rejit::MatchAllCount(dna8mers[index], text);
  }
}


void thread_find_iub_codes() {
  unsigned index;
  while ((index = processed_iub_codes++) < n_iub_codes) {
    rejit::MatchAll(iub_codes[2 * index], text, &iub_codes_matches[index]);
  }
}


bool cmp_match_early(rejit::Match a, rejit::Match b) {
  return a.begin < b.begin;
}


int main() {
  char* text_raw;
  size_t text_raw_size, text_size, replaced_text_size;

  // Find the number of threads to use.
  unsigned n_threads = thread::hardware_concurrency();
  if (n_threads == 0) n_threads = 4;
  threads = reinterpret_cast<thread*>(malloc(n_threads * sizeof(thread)));

  // Initialize the text.
  fseek(stdin, 0, SEEK_END);
  text_raw_size = ftell(stdin);
  text_raw = (char*)malloc(text_raw_size + 1);
  rewind(stdin);
  fread(text_raw, 1, text_raw_size, stdin);
  text.assign(text_raw, text_raw_size);
  free(text_raw);

  rejit::ReplaceAll(">.*\n|\n", text, "");
  text_size = text.size();

  // Count all mers.
  for (unsigned i = 0; i < n_threads; i++) {
    threads[i] = thread(thread_count_mers);
  }
  for (unsigned i = 0; i < n_threads; i++) {
    threads[i].join();
  }
  for (unsigned i = 0; i < n_mers; i++) {
    printf("%s %d\n", dna8mers[i], counts[i]);
  }

  // Search for all iub_codes to replace.
  for (unsigned i = 0; i < n_threads; i++) {
    threads[i] = thread(thread_find_iub_codes);
  }
  for (unsigned i = 0; i < n_threads; i++) {
    threads[i].join();
  }
  // Merge all iub_codes matches into one vector and sort it.
  vector<rejit::Match> *total_matches = &iub_codes_matches[0];
  size_t total_matches_size = 0;
  for (unsigned i = 0; i < n_iub_codes; i ++) {
    total_matches_size += iub_codes_matches[i].size();
  }
  total_matches->reserve(total_matches_size);
  for (unsigned i = 1; i < n_iub_codes; i ++) {
    total_matches->insert(total_matches->end(),
                          iub_codes_matches[i].begin(), iub_codes_matches[i].end());
  }
  sort(total_matches->begin(), total_matches->end(), cmp_match_early);

  // Replace all matches, looking for the correct replacement in the array.
  vector<rejit::Match>::iterator it;
  string res;
  // We reserve 6 extra characters per replaced match.
  // The replacing strings are on average less than 7 characters, minus one
  // character that is being replaced.
  res.reserve(text.size() + 6 * total_matches->size());
  const char* ptext = text.c_str();
  for (it = total_matches->begin(); it < total_matches->end(); it++) {
    res.append(ptext, (*it).begin - ptext);
    ptext = (*it).end;
    // Replace with the correct pattern.
    for (unsigned i = 0; i < n_iub_codes; i++) {
      if (*(it->begin) == iub_codes[2 * i][0]) {
        res.append(iub_codes[2 * i + 1]);
        break;
      }
    }
  }
  res.append(ptext, text.c_str() + text.size() - ptext);

  replaced_text_size = res.size();

  printf("\n%u\n%u\n%u\n",
         (unsigned)text_raw_size,
         (unsigned)text_size,
         (unsigned)replaced_text_size);

  free(threads);

  return EXIT_SUCCESS;
}
