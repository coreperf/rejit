// Copyright (C) 2014 Alexandre Rames <alexandre@coreperf.com>
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

#include <assert.h>
#include <argp.h>
#include <string.h>

#include "rejit.h"
#include "flags.h"

using namespace rejit;


// Start the enum from the latest argp key used.
enum rejit_flags_option_keys {
  // Hope it does not collide with other keys.
  base_rejit_flag_key = 0x7BAD,
#define ENUM_KEYS(flag_name, r, d) flag_name##_key,
  REJIT_FLAGS_LIST(ENUM_KEYS)
#undef ENUM_KEYS
  after_last_rejit_key,
  first_rejit_flag_key = base_rejit_flag_key + 1
};
#define REJIT_FLAG_OFFSET(flag_name) (flag_name##_key - first_rejit_flag_key)

struct arguments {
  char *regexp;
  MatchType match_type;
  int  rejit_flags;
  bool print_all;
};

struct argp_option options[] =
{
  {"match_type" , 'm' , "all"  , OPTION_ARG_OPTIONAL , "Matching type. One of 'full', 'anywhere', 'first', 'all'. Default: 'all'"},
#define FLAG_OPTION(flag_name, r, d) \
  {#flag_name , flag_name##_key , FLAG_##flag_name ? "1" : "0"   , OPTION_ARG_OPTIONAL , "0 to disable, 1 to enable."},
  REJIT_FLAGS_LIST(FLAG_OPTION)
#undef FLAG_OPTION
  {"print_all" , 'p' , ""  , OPTION_ARG_OPTIONAL , "Force all --print* options to be set."},
  {0}
};

char args_doc[] = "regexp";
char doc[] =
"Disassemble the code generated for the specified regexp and match type.\n" ;
const char *argp_program_bug_address = "<alexandre@coreperf.com>";
error_t parse_opt(int key, char *arg, struct argp_state *state);
struct argp argp = {options, parse_opt, args_doc, doc};


error_t parse_opt(int key, char *arg, struct argp_state *state) {
  struct arguments *arguments = reinterpret_cast<struct arguments*>(state->input);
  switch (key) {
    case 'm': {
      if (!arg)
        break;
      if (strcmp("full", arg) == 0) {
        arguments->match_type = kMatchFull;
      } else if (strcmp("anywhere", arg) == 0) {
        arguments->match_type = kMatchAnywhere;
      } else if (strcmp("first", arg) == 0) {
        arguments->match_type = kMatchFirst;
      } else if (strcmp("all", arg) == 0) {
        arguments->match_type = kMatchAll;
      } else {
        printf("ERROR: Invalid match type\n.");
        argp_usage(state);
      }
      break;
    }

    case 'p': {
      arguments->print_all = true;
      break;
    }

#ifdef BENCH_ENGINE_REJIT
#define FLAG_CASE(flag_name, r, d)                                             \
    case flag_name##_key: {                                                    \
      if (arg) {                                                               \
        unsigned v;                                                            \
        v = stol(arg);                                                         \
        assert(v == 0 || v == 1);                                              \
        arguments->rejit_flags |= v << REJIT_FLAG_OFFSET(flag_name);           \
      }                                                                        \
      break;                                                                   \
    }
    REJIT_FLAGS_LIST(FLAG_CASE)
#undef FLAG_CASE
#endif

    case ARGP_KEY_ARG:
      if (state->arg_num >= 1) {
        argp_usage(state);
      }
      arguments->regexp = arg;
      break;
    case ARGP_KEY_END:
      if (state->arg_num < 1) {
        argp_usage(state);
      }
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}


void handle_arguments(struct arguments *arguments,
                      struct argp *argp,
                      int argc,
                      char *argv[]) {
  arguments->regexp = NULL;
  arguments->match_type = kMatchAll;
  arguments->rejit_flags = 0;
  arguments->print_all = false;

#ifdef BENCH_ENGINE_REJIT
#define SET_FLAG_DEFAULT(flag_name, r, d)                                      \
  arguments->rejit_flags |= FLAG_##flag_name << REJIT_FLAG_OFFSET(flag_name);
  arguments->rejit_flags = 0;
  REJIT_FLAGS_LIST(SET_FLAG_DEFAULT)
#undef SET_FLAG_DEFAULT
#endif

  argp_parse(argp, argc, argv, 0, 0, arguments);

  if (arguments->regexp == NULL || arguments->regexp[0] == '\0') {
    printf("ERROR: Cannot test an empty regular expression.\n");
    argp_usage(NULL);
  }

  // Set rejit flags according to the arguments.
#define MAYBE_SET_REJIT_FLAG(flag_name, r, d)                                  \
  SET_FLAG(flag_name,                                                          \
           arguments->rejit_flags & (1 << REJIT_FLAG_OFFSET(flag_name)));
  REJIT_FLAGS_LIST(MAYBE_SET_REJIT_FLAG)
#undef MAYBE_SET_REJIT_FLAG

  if (arguments->print_all) {
#define SET_REJIT_FLAG(flag_name, r, d)                                        \
  SET_FLAG(flag_name, 1);
    REJIT_PRINT_FLAGS_LIST(SET_REJIT_FLAG)
#undef SET_REJIT_FLAG
  }
}



int main(int argc, char *argv[]) {
  struct arguments arguments;
  handle_arguments(&arguments, &argp, argc, argv);

  // Ensure that compilation will dump the generated code.
  SET_FLAG(dump_code, 1);
  rejit::Regej re(arguments.regexp);
  bool success = re.Compile(arguments.match_type);

  if (!success)
    printf("%s\n", rejit_status_string);

  return !success;
}
