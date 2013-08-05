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

#ifndef REJIT_CHECKS_H
#define REJIT_CHECKS_H

#include <assert.h>
#include <stdarg.h>

void rejit_fatal(const char* file, int line, const char* format, ...);

#define STATIC_ASSERT(condition) static_assert((condition), "static_assert")
#define ALWAYS_ASSERT(condition) assert((condition))
#define FATAL(s) rejit_fatal(__FILE__, __LINE__, "FATAL: %s\n", s)

#ifdef DEBUG
#define ASSERT(condition) assert((condition))
#define UNREACHABLE() rejit_fatal(__FILE__, __LINE__, "UNREACHABLE\n")
#define UNIMPLEMENTED() rejit_fatal(__FILE__, __LINE__, "UNIMPLEMENTED\n")
#else
#define ASSERT(condition)
#define UNREACHABLE() rejit_fatal("", 0, "UNREACHABLE\n")
#define UNIMPLEMENTED() rejit_fatal("", 0, "UNIMPLEMENTED\n")
#endif

#endif  // REJIT_CHECKS_H
