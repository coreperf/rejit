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

#ifndef ENGINES_UTILS_H_
#define ENGINES_UTILS_H_

#include <iostream>

using namespace std;

// sec
inline void print_speed(int64_t sec, int64_t usec, size_t file_size, unsigned times) {
  double time_usec = (double)usec + (double)sec * 1000000.0;
  cout << ((double)file_size / time_usec) * 1000000.0 * (double)times << endl;
}


#endif
