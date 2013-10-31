#include "bench_engine.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>

#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>


struct argp_option options[] =
{
  {"file"       , 'f' , ""     , OPTION_ARG_OPTIONAL , "Source file. If none provided, use a randomly generated characters."}, 
  {"size"       , 's' , "65536", OPTION_ARG_OPTIONAL , "Comma-separated list of text sizes."},
  {"iterations" , 'i' , "1000" , OPTION_ARG_OPTIONAL , "Number of iterations to run."},
  {"low_char"   , 'l' , "0"    , OPTION_ARG_OPTIONAL , "When the match source is random text, the low character of the range of characters composing the matched text."}, 
  {"high_char"  , 'h' , "z"    , OPTION_ARG_OPTIONAL , "When the match source is random text, the high character of the range of characters composing the matched text."}, 
#ifdef BENCH_ENGINE_REJIT
  // Convenient access to rejit flags.
#define FLAG_OPTION(flag_name, r, d) \
  {#flag_name , flag_name##_key , FLAG_##flag_name ? "1" : "0"   , OPTION_ARG_OPTIONAL , "0 to disable, 1 to enable."},
  REJIT_FLAGS_LIST(FLAG_OPTION)
#undef FLAG_OPTION
#endif
  {0}
};

char doc[] =
"\n"
"Benchmark regular expression engine.\n"
"\n"
"Output: processing speed in bytes/s (<size of text matched> / <time to match>)\n"
"\t<worst speed> (1 run for 1 compilation)\n"
"\t<amortised speed> (<--iterations=?> runs for 1 compilation)\n"
"\t<best speed> (without considering compilation time)\n";

char args_doc[] = "regexp";

const char *argp_program_bug_address = "<alexandre@coreperf.com>";

struct argp argp = {options, parse_opt, args_doc, doc};


error_t parse_opt(int key, char *arg, struct argp_state *state) {
  struct arguments *arguments = reinterpret_cast<struct arguments*>(state->input);
  switch (key) {
    case 'f':
      arguments->file = arg;
      break;
    case 's': {
      size_t pos;
      size_t val;
      if (!arg)
        break;
      while (arg[0] != '\0') {
        val = stoll(arg, &pos);
        if (pos == 0)
          break;
        arguments->sizes.push_back(val);
        arg += pos + (arg[pos] == ',');
      }

      if (arguments->sizes.size() == 0 || arg[0] != '\0')
        error("Invalid sizes arguments.");

      break;
    }
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
  arguments->iterations = 1000;
  arguments->low_char   = 'a';
  arguments->high_char  = 'z';

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


  if (arguments->sizes.size() == 0)
    arguments->sizes.push_back(65536);
  else
    sort(arguments->sizes.begin(), arguments->sizes.end());
}


void prepare_text(struct arguments *arguments, string *text) {
  size_t max_text_size = arguments->sizes.back();
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


void print_results(vector<bench_res> *results)
{
  vector<bench_res>::iterator it;
  unsigned max_width = strlen("text_size");

  for (it = results->begin(); it < results->end(); it++) {
    unsigned width = 0;
    size_t n = (*it).text_size;
    while (n) {
      n /= 10;
      ++width;
    }
    max_width = max(max_width, width);
  }

  cout <<
    setw(max_width) << "text_size" <<
    setw(16) << "worse" <<
    setw(16) << "amortised" <<
    setw(16) << "best" << endl;
  for (it = results->begin(); it < results->end(); it++) {
  cout <<
    setw(max_width) << (*it).text_size <<
    setw(16) << (*it).worse <<
    setw(16) << (*it).amortised <<
    setw(16) << (*it).best << endl;
  }
}


double speed(struct timeval *t0, struct timeval *t1, size_t text_size, unsigned times) {
  int64_t sec = t1->tv_sec - t0->tv_sec;
  int64_t usec = t1->tv_usec - t0->tv_usec;
  double time_usec = (double)usec + (double)sec * 1000000.0;
  return ((double)text_size / time_usec) * 1000000.0 * (double)times;
}


void error(const char* message, int rc) {
  printf("ERROR: %s\nExiting.\n", message);
  exit(rc);
}
