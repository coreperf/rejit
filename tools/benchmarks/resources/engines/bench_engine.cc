#include "bench_engine.h"

#include <algorithm>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>


struct argp_option options[] =
{
  {"file"       , 'f' , ""     , OPTION_ARG_OPTIONAL , "Source file. If none provided, use a randomly generated characters."}, 
  {"size"       , 's' , "65536", OPTION_ARG_OPTIONAL , "Size of the text to match."},
  {"iterations" , 'i' , "1000" , OPTION_ARG_OPTIONAL , "Number of iterations to run."},
  {"low_char"   , 'l' , "0"    , OPTION_ARG_OPTIONAL , "When the match source is random text, the low character of the range of characters composing the matched text."}, 
  {"high_char"  , 'h' , "z"    , OPTION_ARG_OPTIONAL , "When the match source is random text, the high character of the range of characters composing the matched text."}, 
  {"match_type" , 'm' , "all"  , OPTION_ARG_OPTIONAL , "Type of matching to perform. [all, first]."}, 
#ifdef BENCH_ENGINE_REJIT
  // Convenient access to rejit flags.
#define FLAG_OPTION(flag_name, r, d) \
  {#flag_name , flag_name##_key , FLAG_##flag_name ? "1" : "0"   , OPTION_ARG_OPTIONAL , "0 to disable, 1 to enable."},
  REJIT_FLAGS_LIST(FLAG_OPTION)
#undef FLAG_OPTION
  {0}
#endif
};

char doc[] =
"\n"
"Benchmark regular expression engine.\n"
"\n"
"Output: processing speed in bytes/s (<size of text matched> / <time to match>)\n"
"\t<worst speed> (1 run for 1 compilation)\n"
"\t<amortised speed> (<--iterations=?> runs for 1 compilation)\n"
"\t<best speed> (without considering compilation time)\n"
"When benchmarking using --match_type=first, be careful that your regular "
"expression does not match or you wil end up with surprising performance "
"results!";

char args_doc[] = "regexp";

const char *argp_program_bug_address = "<alexandre@coreperf.com>";

struct argp argp = {options, parse_opt, args_doc, doc};


error_t parse_opt(int key, char *arg, struct argp_state *state) {
  struct arguments *arguments = reinterpret_cast<struct arguments*>(state->input);
  switch (key) {
    case 'f':
      arguments->file = arg;
      break;
    case 's':
      if (arg)
        arguments->size = stoll(arg);
      break;
    case 'i':
      if (arg) {
        arguments->iterations = stol(arg);
        if (arguments->iterations == 0) {
          error("The number of iterations to run must be greater than 0.");
        }
      }
      break;
    case 'l':
      arguments->low_char = arg[0];
      break;
    case 'h':
      arguments->high_char = arg[0];
      break;
    case 'm':
      if (strcmp(arg, "all") == 0) {
        arguments->match_type = kMatchAll;
      } else if (strcmp(arg, "first") == 0) {
        arguments->match_type = kMatchFirst;
      } else {
        argp_usage(state);
      }
      break;

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
      arguments->args[state->arg_num] = arg;
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
  arguments->file       = NULL;
  arguments->size       = 65536;
  arguments->iterations = 1000;
  arguments->low_char   = 'a';
  arguments->high_char  = 'z';
  arguments->match_type = kMatchAll;

#ifdef BENCH_ENGINE_REJIT
#define SET_FLAG_DEFAULT(flag_name, r, d)                                      \
  arguments->rejit_flags |= FLAG_##flag_name << REJIT_FLAG_OFFSET(flag_name);
  arguments->rejit_flags = 0;
  REJIT_FLAGS_LIST(SET_FLAG_DEFAULT)
#undef SET_FLAG_DEFAULT
#endif

  argp_parse(argp, argc, argv, 0, 0, arguments);

  if (arguments->args[0] == '\0') {
    error("Cannot test an empty regular expression.");
  }
}


void prepare_text(struct arguments *arguments, string *text) {
  size_t max_text_size = arguments->size;
  if (arguments->file) {
    // Use the content of the specified file to fill text. If the file is
    // smaller than the requested test size, copy it multiple times.
    struct stat file_stats;
    int fd = open(arguments->file, O_RDONLY);
    fstat(fd, &file_stats);
    char* file_text = (char*)mmap(NULL, file_stats.st_size,
                                  PROT_READ, MAP_PRIVATE, fd, 0);
    // Avoid the terminating character.
    size_t file_text_size = file_stats.st_size - 1;
    // Fill the text to search.
    // If the source file is not big enough, loop over it.
    size_t offset = 0;
    size_t copy_size;
    while (offset < max_text_size) {
      copy_size = min(max_text_size - offset, file_text_size);
      text->append(file_text, copy_size);
      offset += copy_size;
    }

    munmap(file_text, file_stats.st_size);

  } else {
    text->resize(max_text_size);
    for (size_t i = 0; i < max_text_size - 1; i++) {
      text->at(i) = arguments->low_char +
        (rand() % (arguments->high_char - arguments->low_char));
    }
  }
}


void print_speed(int64_t sec, int64_t usec, size_t file_size, unsigned times) {
  double time_usec = (double)usec + (double)sec * 1000000.0;
  cout << ((double)file_size / time_usec) * 1000000.0 * (double)times << endl;
}


void error(const char* message, int rc) {
  printf("ERROR: %s\nExiting.\n", message);
  exit(rc);
}
