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

#include "rejit.h"

using namespace std;


// Argp configuration ----------------------------------------------------------
const char *argp_program_version = "grep-like sample powered by rejit";
const char *argp_program_bug_address = "<alexandre@coreperf.com>";

/* This structure is used by main to communicate with parse_opt. */
struct arguments {
  // TODO: Use a vector to store file names and allow using multiple files.
  char *regexp;
  vector<char*> filenames;
  bool print_filename;
  bool print_line_number;
  bool recursive;
  int nopenfd;
  int context_before;
  int context_after;
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
  {"recursive", 'R', NULL, OPTION_ARG_OPTIONAL,
    "Recursively search directories listed."},
  {NULL, 'r', NULL, OPTION_ALIAS, NULL},
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
    case 'H':
      arguments->print_filename = true;
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
    case 'r':
      arguments->recursive = true;
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
"One additional feature is the ability to use patterns matching over multiple\n"
"lines (eg. \"a\\nb\").\n";
static struct argp argp = {options, parse_opt, args_doc, doc};


// Processing code -------------------------------------------------------------

// Declared global to be easily accessed via ftw's callback.
rejit::Regej *re;


bool is_dir(const char* name) {
  int rc;
  struct stat file_stats;

  rc = stat(name, &file_stats);
  if (rc) {
    printf("jrep: %s: %s\n", name, strerror(errno));
    exit(rc);
  }
  return file_stats.st_mode & S_IFDIR;
}


static void print_head(const char* filename,
                       unsigned line,
                       char separator=':') {
  if (arguments.print_filename) {
    printf("%s%c", filename, separator);
  }
  if (arguments.print_line_number) {
    // Index starts at 0, but line numbering at 1.
    printf("%d%c", line, separator);
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
    rejit::MatchAll("^", file_content, file_size, &new_lines);
    // Append a match for the end of the file to be able to correctly print the
    // last line.
    new_lines.push_back({file_content + file_size, file_content + file_size});

    vector<rejit::Match>::iterator it_lines = new_lines.begin();
    vector<rejit::Match>::iterator it_matches = matches.begin();
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
        printf("--\n");
        vector<rejit::Match>::iterator it;
        for(it = max(it_lines - arguments.context_before, new_lines.begin());
            it < it_lines;
            it++) {
          print_head(filename, (int)(1 + (it - new_lines.begin())), '-');
          printf("%.*s", (int)((it + 1)->begin - it->begin), it->begin);
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
        printf("%.*s" START_RED "%.*s" END_COLOR,
               (int)(it_matches->begin - start), start,
               (int)(it_matches->end - it_matches->begin), it_matches->begin);
        start = it_matches->end;
        ++it_matches;
      }
      // And print the rest of the line for the last match.
      vector<rejit::Match>::iterator it_end_lines = it_lines;
      while (it_end_lines->begin < (it_matches - 1)->end) {
        ++it_end_lines;
      }
      printf("%.*s",
             (int)((it_end_lines)->end - (it_matches - 1)->end),
             (it_matches - 1)->end);

      if (arguments.context_after) {
        // Print the 'context_after'.
        for(vector<rejit::Match>::iterator it = it_end_lines;
            it < min(it_end_lines + arguments.context_after, new_lines.end());
            it++) {
          print_head(filename, (int)(1 + (it - new_lines.begin())), '-');
          printf("%.*s", (int)((it + 1)->begin - it->begin), it->begin);
        }
        printf("--\n");
      }
    }
  }

  munmap(file_content, file_size);
close_file:
  close(fd);
exit:
  return rc;
}


int ftw_callback(const char *path, const struct stat *s, int typeflag) {
  int rc;
  // The regexp and arguments are global to easily be accessed via this callback.
  if (typeflag == FTW_F) {
    rc = process_file(path);
    if (rc) {
      // Print an error message and continue.
      printf("jrep: %s: %s\n", path, strerror(rc));
    }
  }
  return 0;
}


int process_directory(const char* dirname) {
  // Use ftw to walk the file tree.
  return ftw(dirname, ftw_callback, arguments.nopenfd);
}


int process_file_or_dir(const char* name) {
  int rc;
  struct stat file_stats;

  rc = stat(name, &file_stats);
  if (rc) {
    printf("jrep: %s: %s\n", name, strerror(errno));
    return rc;
  }

  if (file_stats.st_mode & S_IFDIR) {
    return process_directory(name);
  } else if (file_stats.st_mode & S_IFREG) {
    return process_file(name);
  }

  return EXIT_SUCCESS;
}


int main(int argc, char *argv[]) {
  int rc;
  // Arguments parsing -------------------------------------
  memset(&arguments, 0, sizeof(arguments));
  arguments.nopenfd = 1024;
  argp_parse(&argp, argc, argv, 0, 0, &arguments);

  if (arguments.regexp[0] == 0) {
    return EXIT_SUCCESS;
  }

  rejit::Regej re_(arguments.regexp);
  re_.Compile(rejit::kMatchAll);

  re = &re_;

  vector<char*>::iterator it;
  for (it = arguments.filenames.begin(); it < arguments.filenames.end(); it++) {
    const char* name = *it;
    if (!arguments.recursive && is_dir(name)) {
      printf("jrep: %s: Is a directory.\n", name);
    }
    rc = process_file_or_dir(name);
    if (rc != EXIT_SUCCESS) {
      return rc;
    }
  }

  return EXIT_SUCCESS;
}
