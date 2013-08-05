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


#include "rejit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <iostream>

#include <pthread.h>

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

// Use global variables to simplify the implementation of the thread function.
const unsigned n_mers = sizeof(dna8mers) / sizeof (dna8mers[0]);
unsigned counts[n_mers];
string text;

void* match_mer(void *i_) {
  unsigned i = reinterpret_cast<unsigned long>(i_);
  counts[i] = rejit::MatchAllCount(dna8mers[i], text);
  return NULL;
}

int main() {

  char* text_raw;
  size_t text_raw_size, text_size, replaced_text_size;

  pthread_t thread[n_mers];

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

  // Count all dna mers in parallel.
  for (unsigned i = 0; i < n_mers; i++) {
    pthread_create(&thread[i], NULL, match_mer, reinterpret_cast<void*>(i));
  }
  for (unsigned i = 0; i < n_mers; i++) {
    pthread_join(thread[i], NULL);
  }
  for (unsigned i = 0; i < n_mers; i++) {
    printf("%s %d\n", dna8mers[i], counts[i]);
  }

  // Replace codes.
  // The benchmark forbids us from searching for all the codes and replacing all in one pass.
  for (unsigned i = 0; i < sizeof(iub_codes) / sizeof(char*); i += 2) {
    rejit::ReplaceAll(iub_codes[i], text, iub_codes[i + 1]);
  }
  replaced_text_size = text.size();

  printf("\n%u\n%u\n%u\n",
         (unsigned)text_raw_size,
         (unsigned)text_size,
         (unsigned)replaced_text_size);

  return EXIT_SUCCESS;
}
