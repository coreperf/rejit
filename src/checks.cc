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

#include "checks.h"
#include <stdio.h>
#include <stdlib.h>

void rejit_fatal(const char* file, int line, const char* format, ...) {
  printf("ERROR: in %s, line %d: ", file, line);
  va_list args;
  va_start(args, format);
  printf(format, args);
  va_end(args);
  abort();
}
