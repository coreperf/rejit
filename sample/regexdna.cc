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
#include <iostream>

using namespace std;

int main(int argc, char **argv) {

  char* text_raw;
  string text;
  size_t text_raw_size, text_size, replaced_text_size;

  if (argc > 1) {
    printf(
        "Usage:\n"
        "  regexdna < input.file\n"
        "You can generate input files using the fasta program available from\n"
        "  http://benchmarksgame.alioth.debian.org/u64/benchmark.php?test=fasta&lang=gcc&id=1&data=u64"
        );
    return EXIT_SUCCESS;
  }

  fseek(stdin, 0, SEEK_END);
  text_raw_size = ftell(stdin);
  text_raw = (char*)malloc(text_raw_size + 1);
  rewind(stdin);
  fread(text_raw, 1, text_raw_size, stdin);
  text.assign(text_raw, text_raw_size);
  free(text_raw);

  rejit::ReplaceAll(">.*\n|\n", text, "");
  text_size = text.size();

  const char* dna8mers[] = {
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

  for (unsigned i = 0; i < sizeof(dna8mers) / sizeof(char*); i++) {
    unsigned count = rejit::MatchAllCount(dna8mers[i], text);
    printf("%s %d\n", dna8mers[i], count);
  }

  const char* iub_codes[] = {
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
