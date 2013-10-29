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

#ifndef BENCH_ENGINE_H_
#define BENCH_ENGINE_H_

#include <iostream>
#include <vector>

#include <argp.h>


#ifdef BENCH_ENGINE_REJIT
#include "rejit.h"
#include "checks.h"
#include "flags.h"
using namespace rejit;
#endif


using namespace std;


#ifndef BENCH_ENGINE_REJIT
// Copied from rejit.h
enum MatchType {
  kMatchFull,
  kMatchAnywhere,
  kMatchFirst,
  kMatchAll,
  kNMatchTypes
};
#endif

#ifdef BENCH_ENGINE_REJIT
// Start the enum from the latest argp key used.
enum rejit_flags_option_keys {
  last_argp_key = ARGP_KEY_FINI,
#define ENUM_KEYS(flag_name, r, d) flag_name##_key,
  REJIT_FLAGS_LIST(ENUM_KEYS)
#undef ENUM_KEYS
  first_rejit_flag_key = last_argp_key + 1
};
#define REJIT_FLAG_OFFSET(flag_name) (flag_name##_key - first_rejit_flag_key)
#endif


struct arguments {
  char      *args[1];  // The regexp.
  char      *file;
  size_t    size;
  unsigned  iterations;
  char      low_char;
  char      high_char;
  MatchType match_type;
#ifdef BENCH_ENGINE_REJIT
  int  rejit_flags;
#endif
};

extern struct argp_option options[];
extern char args_doc[];
extern char doc[];
extern struct argp argp;
extern const char *argp_program_bug_address;

error_t parse_opt(int key, char *arg, struct argp_state *state);

void handle_arguments(struct arguments *arguments,
                      struct argp *argp,
                      int argc,
                      char *argv[]);

void prepare_text(struct arguments *arguments, string *text);


void error(const char* message, int rc = 1);

void print_speed(int64_t sec, int64_t usec, size_t file_size, unsigned times);

#endif
