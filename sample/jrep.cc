// Copyright (C) 2013 Alexandre Rames <alexandre@coreperf.com>
//
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



#include <argp.h>

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ftw.h>
#include <string.h>
#include <vector>
#include <cerrno>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

#ifndef REJIT_TARGET_PLATFORM_MACOS
// TODO: Unify output method on Linux and OSX.
// cout showed a significant performance gain on Linux (~30% faster), whereas it
// showed a hugh performance loss on OSX (about 4 times slower). Duplicating the
// output statements is aweful, but the performance gain is too important to be
// ignored.
#include <iostream>
#endif

#include "rejit.h"

using namespace std;

// Multithreading --------------------------------------------------------------
// If <n> regular expression processing threads are allowed, one thread
// walks the file tree (and processes files if <n> is zero) while <n> others
// process the files.

// The names of files to process are stored in this array.
// We use C++ string to automate the memory management.
string *filenames;
unsigned n_filenames;
atomic_uint fn_written = ATOMIC_VAR_INIT(0), fn_read = ATOMIC_VAR_INIT(0);
mutex fn_mutex;
condition_variable fn_need_refill;
condition_variable fn_refilled;
volatile bool fn_done_listing = false;
thread **threads;

// Protects printing to the output.
mutex output_mutex;


// Argp configuration ----------------------------------------------------------
const char *argp_program_version = "grep-like sample powered by rejit";
const char *argp_program_bug_address = "<alexandre@coreperf.com>";

enum {
  NOT_RECURSIVE = 0,
  RECURSIVE,  // Do not follow symlinks.
  RECURSIVE_FOLLOW_SYMLINKS
};

/* This structure is used by main to communicate with parse_opt. */
struct arguments {
  char *regexp;
  vector<char*> filenames;
  bool print_filename;
  bool print_line_number;
  int recursive_search;
  // TODO: Automatically handle coloring. This was protected behind an option to
  // not trash the output when redirected to a file.
  bool color_output;
  unsigned jobs;
  unsigned nopenfd;
  unsigned context_before;
  unsigned context_after;
} arguments;

/*
   OPTIONS.  Field 1 in ARGP.
   Order of fields: {NAME, KEY, ARG, FLAGS, DOC}.
*/
const unsigned group_context = 1;
static struct argp_option options[] = {
  {NULL, 'H', NULL, OPTION_ARG_OPTIONAL,
    "Print the filename with output lines."},
  {"line-number", 'n', NULL, OPTION_ARG_OPTIONAL,
    "Print the line number of matches with output lines."},
  {"recursive", 'r', NULL, OPTION_ARG_OPTIONAL,
    "Recursively search directories. Do not follow symbolic links."},
  {NULL, 'R', NULL, OPTION_ARG_OPTIONAL,
    "Recursively search directories. Follow symbolic links."},
  {"color_output", 'c', NULL, OPTION_ARG_OPTIONAL,
    "Highlight matches in red."},
  {"jobs", 'j', "0", OPTION_ARG_OPTIONAL,
    "Specify the number <n> of regular expression processing threads to use.\n"
    "One thread walks the file tree (and process files if <n> is zero), while"
    "<n> others process the files."
  },
  {"nopenfd", 'k', "1024", OPTION_ARG_OPTIONAL,
    "The maximum number of directories that ftw() can hold open simultaneously."
  },
  {"after-context", 'A', "0", OPTION_ARG_OPTIONAL,
    "Print <n> lines of context after every match. See also -B and -C options.",
    group_context},
  {"before-context", 'B', "0", OPTION_ARG_OPTIONAL,
    "Print <n> lines of context after every match. See also -A and -C options.",
    group_context},
  {"context", 'C', "0", OPTION_ARG_OPTIONAL,
    "Print <n> lines of context before and after every match."
    "See also -A and -B options.",
    group_context},
  {0}
};

static int argtoi(const char *arg) {
  while (*arg == ' ') arg++;
  return atoi(arg);
}

static error_t
parse_opt(int key, char *arg, struct argp_state *state) {
  struct arguments *arguments = reinterpret_cast<struct arguments*>(state->input);
  switch (key) {
    case 'A':
      if (arg) {
        arguments->context_after = argtoi(arg);
      }
      break;
    case 'B':
      if (arg) {
        arguments->context_before = argtoi(arg);
      }
      break;
    case 'C':
      if (arg) {
        arguments->context_after = argtoi(arg);
        arguments->context_before = argtoi(arg);
      }
      break;
    case 'c':
      arguments->color_output = true;
      break;
    case 'H':
      arguments->print_filename = true;
      break;
    case 'j':
      if (arg) {
        arguments->jobs = argtoi(arg);
      }
      break;
    case 'k':
      if (arg) {
        arguments->nopenfd = argtoi(arg);
      }
      break;
    case 'n':
      arguments->print_line_number = true;
      break;
    case 'R':
      arguments->recursive_search = RECURSIVE_FOLLOW_SYMLINKS;
      break;
    case 'r':
      arguments->recursive_search = RECURSIVE;
      break;
    case ARGP_KEY_ARG:
      switch (state->arg_num) {
        case 0:
          arguments->regexp = arg;
          break;
        default:
          arguments->filenames.push_back(arg);
      }
      break;
    case ARGP_KEY_END:
      if (state->arg_num < 2) {
        argp_usage(state);
      }
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static char args_doc[] = "regexp file";
static char doc[] =
"grep-like program powered by rejit.\n"
"\n"
"It provides only a *tiny* subset of grep features.\n"
"Two additional features:\n"
"  - you can search for multi-lines patterns (eg. \"a\\nb\").\n"
"  - there is initial support for multi-threading.\n";
static struct argp argp = {options, parse_opt, args_doc, doc};


// Processing code -------------------------------------------------------------

// Declared global to be easily accessed via ftw's callback.
rejit::Regej *re;
rejit::Regej re_sol("^");


bool is_dir(const char* name) {
  int rc;
  struct stat file_stats;

  rc = stat(name, &file_stats);
  if (rc) {
    fprintf(stderr, "jrep: %s: %s\n", name, strerror(errno));
    exit(rc);
  }
  return file_stats.st_mode & S_IFDIR;
}


static void print_head(const char* filename,
                       unsigned line,
                       char separator=':') {
  if (arguments.print_filename) {
#ifdef REJIT_TARGET_PLATFORM_MACOS
    printf("%s%c", filename, separator);
#else
    cout << filename << separator;
#endif
  }
  if (arguments.print_line_number) {
    // Index starts at 0, but line numbering at 1.
#ifdef REJIT_TARGET_PLATFORM_MACOS
    printf("%d%c", line, separator);
#else
    cout << line << separator;
#endif
  }
}


int process_file(const char* filename) {
  int rc = EXIT_SUCCESS;

  size_t file_size;
  char *file_content;

  vector<rejit::Match> matches;
  struct stat file_stats;

  int fd = open(filename, O_RDONLY);
  if (fd < 0) {
    rc = errno;
    errno = 0;
    goto exit;
  }
  fstat(fd, &file_stats);
  file_size = file_stats.st_size;
  if (file_size == 0) {
    goto close_file;
  }
  file_content = (char*)mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (errno) {
    rc = errno;
    errno = 0;
    goto close_file;
  }

  re->MatchAll(file_content, file_size, &matches);

  if (matches.size()) {
    // TODO: When not printing line numbers it may be faster to look for sos and
    // eos only for each match.
    vector<rejit::Match> new_lines;
    re_sol.MatchAll(file_content, file_size, &new_lines);
    // Append a match for the end of the file to be able to correctly print the
    // last line.
    new_lines.push_back({file_content + file_size, file_content + file_size});

    vector<rejit::Match>::iterator it_lines = new_lines.begin();
    vector<rejit::Match>::iterator it_matches = matches.begin();
    output_mutex.lock();
    while (it_lines < new_lines.end() && it_matches < matches.end()) {
      // Accesses at the limits of the vectors look a bit dangerous but should
      // be guaranteed because the matches are strictly included between the
      // first and last elements of new_lines (respectively start of the first
      // line and end of the last line).

      // Find the sol before the next match.
      while(it_lines->begin <= it_matches->begin &&
            it_lines < new_lines.end()) {
        ++it_lines;
      }
      --it_lines;

      if (arguments.context_before) {
        // Print the 'context_before'.
#ifdef REJIT_TARGET_PLATFORM_MACOS
        printf("--\n");
#else
        cout << "--\n";
#endif
        vector<rejit::Match>::iterator it;
        for(it = max(it_lines - arguments.context_before, new_lines.begin());
            it < it_lines;
            it++) {
          print_head(filename, (int)(1 + (it - new_lines.begin())), '-');
#ifdef REJIT_TARGET_PLATFORM_MACOS
          printf("%.*s", (int)((it + 1)->begin - it->begin), it->begin);
#else
          cout.write(it->begin, (int)((it + 1)->begin - it->begin));
#endif
        }
      }

      // Print the filename and line number.
      print_head(filename, (int)(1 + (it_lines - new_lines.begin())));
#define START_RED "\x1B[31m"
#define END_COLOR "\x1B[0m"
      // Now print all matches starting on this line.
      const char *start = it_lines->begin;
      if (start == new_lines.back().begin) {
        ++it_matches;
        continue;
      }
      while (it_matches < matches.end() &&
             it_matches->begin < (it_lines + 1)->begin) {
#ifdef REJIT_TARGET_PLATFORM_MACOS
        printf(arguments.color_output ? "%.*s" START_RED "%.*s" END_COLOR : "%.*s%.*s" ,
               (int)(it_matches->begin - start), start,
               (int)(it_matches->end - it_matches->begin), it_matches->begin);
#else
        cout.write(start, (int)(it_matches->begin - start));
        cout.write(it_matches->begin, (int)(it_matches->end - it_matches->begin));
#endif
        start = it_matches->end;
        ++it_matches;
      }
      // And print the rest of the line for the last match.
      vector<rejit::Match>::iterator it_end_lines = it_lines;
      while (it_end_lines->begin < (it_matches - 1)->end) {
        ++it_end_lines;
      }
#ifdef REJIT_TARGET_PLATFORM_MACOS
      printf("%.*s",
             (int)((it_end_lines)->end - (it_matches - 1)->end),
             (it_matches - 1)->end);
#else
      cout.write((it_matches - 1)->end, (int)((it_end_lines)->end - (it_matches - 1)->end));
#endif

      if (arguments.context_after) {
        // Print the 'context_after'.
        vector<rejit::Match>::iterator it;
        for(it = it_end_lines;
            it < min(it_end_lines + arguments.context_after, new_lines.end() - 1);
            it++) {
          print_head(filename, (int)(1 + (it - new_lines.begin())), '-');
#ifdef REJIT_TARGET_PLATFORM_MACOS
          printf("%.*s", (int)((it + 1)->begin - it->begin), it->begin);
#else
          cout.write(it->begin, (int)((it + 1)->begin - it->begin));
#endif
        }
#ifdef REJIT_TARGET_PLATFORM_MACOS
        if (it == new_lines.end() - 1) {
          printf("\n");
        }
        printf("--\n");
#else
        if (it == new_lines.end() - 1) {
          cout << "\n";
        }
        cout << "--\n";
#endif
      }
    }
  output_mutex.unlock();
  }

  munmap(file_content, file_size);
close_file:
  close(fd);
exit:
  return rc;
}


int list_file(const char *filename) {
  if (arguments.jobs > 0) {
    unique_lock<mutex> fn_lock(fn_mutex);
    unsigned index;
    if (fn_written >= fn_read + n_filenames) {
      fn_need_refill.wait(fn_lock);
    }
    index = fn_written++;
    filenames[index % n_filenames].assign(filename);
    fn_refilled.notify_one();
    fn_lock.unlock();
    return 0;
  } else {
    return process_file(filename);
  }
}


int ftw_callback(const char *path, const struct stat *s, int typeflag,
                 struct FTW *unused) {
  if (typeflag == FTW_F) {
    if (arguments.jobs > 0) {
      list_file(path);
    } else {
      return process_file(path);
    }
  }
  return 0;
}


inline int list_directory(const char* dirname) {
  int visit_symlinks =
    arguments.recursive_search == RECURSIVE_FOLLOW_SYMLINKS ? 0 : FTW_PHYS;
  return nftw(dirname, ftw_callback, arguments.nopenfd, visit_symlinks);
}


int list_file_or_dir(const char* name) {
  int rc;
  struct stat file_stats;

  rc = stat(name, &file_stats);
  if (rc) {
    fprintf(stderr, "jrep: %s: %s\n", name, strerror(errno));
    return rc;
  }

  if (file_stats.st_mode & S_IFDIR) {
    return list_directory(name);
  } else if (file_stats.st_mode & S_IFREG) {
    return list_file(name);
  }

  return EXIT_SUCCESS;
}


void job_process_files() {
  string filename;
  unique_lock<mutex> fn_lock(fn_mutex, defer_lock);

  while (!fn_done_listing || fn_read < fn_written) {
    fn_lock.lock();
    if (fn_read >= fn_written) {
      fn_refilled.wait(fn_lock);
    }
    if (fn_read < fn_written) {
      // We need to locally copy the filename, which could be overwritten in the
      // array.
      filename.assign(filenames[fn_read++ % n_filenames]);
      fn_lock.unlock();
      // Avoid waking up the listing thread if there are still enough files to
      // process.
      if (fn_written - fn_read < n_filenames / 2) {
        fn_need_refill.notify_one();
      }
      process_file(filename.c_str());
    } else {
      fn_lock.unlock();
    }
  }
}

int main(int argc, char *argv[]) {
  int rc;
  // Arguments parsing -------------------------------------
  memset(&arguments, 0, sizeof(arguments));
  arguments.nopenfd = 1024;
  arguments.jobs = 0;
  argp_parse(&argp, argc, argv, 0, 0, &arguments);

  if (arguments.regexp[0] == 0) {
    return EXIT_SUCCESS;
  }

  rejit::Regej re_(arguments.regexp);
  re_.Compile(rejit::kMatchAll);
  re = &re_;

  if (arguments.jobs > 0) {
    // Initialize structures for multithreaded processing.
    threads = reinterpret_cast<thread**>(malloc(arguments.jobs * sizeof(thread*)));
    if (!threads) {
      fprintf(stderr, "jrep: %s\n", strerror(errno));
      exit(errno);
    }
    n_filenames = max(arguments.nopenfd, 16 * arguments.jobs);
    filenames = new string[n_filenames];
    if (!filenames) {
      fprintf(stderr, "jrep: %s\n", strerror(errno));
      exit(errno);
    }

    // Start the processing threads.
    for (unsigned i = 0; i < arguments.jobs; i++) {
      threads[i] = new thread(job_process_files);
    }
  }

  // List files to process.
  vector<char*>::iterator it;
  for (it = arguments.filenames.begin(); it < arguments.filenames.end(); it++) {
    const char* name = *it;
    if (!arguments.recursive_search && is_dir(name)) {
      fprintf(stderr, "jrep: %s: Is a directory.\n", name);
      continue;
    }
    rc = list_file_or_dir(name);
    if (rc != EXIT_SUCCESS) {
      return rc;
    }
  }
  fn_done_listing = true;
  atomic_thread_fence(memory_order_seq_cst);
  fn_refilled.notify_all();

  for (unsigned i = 0; i < arguments.jobs; i++) {
    threads[i]->join();
  }

  return EXIT_SUCCESS;
}
