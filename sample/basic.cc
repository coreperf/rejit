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

#include <stdio.h>
#include <stdlib.h>
// For file operations.
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "rejit.h"

using namespace std;

int main() {
  printf("Running basic rejit example.\n");

  const char* regexp = "(right|[ts]ion)";
  const char* text;
  int fd;
  // Get the text in the file.
  struct stat file_stats;
  fd = open("LICENCE", O_RDONLY);
  fstat(fd, &file_stats);
  text = (char*)mmap(NULL, file_stats.st_size,
                     PROT_READ, MAP_PRIVATE, fd, 0);

  printf("Searching for %s in the LICENCE file.\n", regexp);

  rejit::Regej re(regexp);
  vector<rejit::Match> matches;

  re.MatchAll(text, &matches);

  printf("Found %ld matches.\n", matches.size());

  printf("Printing the first 10:\n");
  unsigned i = 0;
  vector<rejit::Match>::iterator it;
  for (it = matches.begin(); it < matches.end() && i++ < 10; it++) {
    const char* c = (*it).begin;
    while (c < (*it).end) {
      printf("%c", *c);
      c++;
    }
    printf("\n");
  }

  munmap((void*)text, file_stats.st_size);

  return EXIT_SUCCESS;
}
