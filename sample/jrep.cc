// Copyright (C) 2013 Alexandre Rames <alexandre@uop.re>
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


#include <stdlib.h>
#include <stdio.h>

// Arguments handling.
#include <argp.h>
// For file operations.
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
// File tree traversal.
#include <ftw.h>

// To handle the list of files to process.
#include <vector>
#include <cerrno>

#include "rejit.h"

using namespace std;


// Argp configuration ----------------------------------------------------------
const char *argp_program_version = "grep-like sample powered by rejit";
const char *argp_program_bug_address = "<alexandre@uop.re>";

/* This structure is used by main to communicate with parse_opt. */
struct arguments {
  // TODO: Use a vector to store file names and allow using multiple files.
  char *regexp;
  vector<char*> filenames;
  bool print_filename;
  bool print_line_number;
  bool recursive;
  int nopenfd;
} arguments;

/*
   OPTIONS.  Field 1 in ARGP.
   Order of fields: {NAME, KEY, ARG, FLAGS, DOC}.
*/
static struct argp_option options[] = {
  {NULL, 'H', NULL, OPTION_ARG_OPTIONAL,
    "Print the filename with output lines."},
  {"line-number", 'n', NULL, OPTION_ARG_OPTIONAL,
    "Print the line number of matches with ouptu lines."},
  {"recursive", 'R', NULL, OPTION_ARG_OPTIONAL,
    "Recursively search directories listed."},
  {NULL, 'r', NULL, OPTION_ALIAS,
    "Recursively search directories listed."},
  {"nopenfd", 'k', "1024", OPTION_ARG_OPTIONAL,
    "The maximum number of directories that ftw() can hold open simultaneously."
  },
  // TODO: Add context options.
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
    case 'H':
      arguments->print_filename = true;
      break;
    case 'n':
      arguments->print_line_number = true;
      break;
    case 'R':
    case 'r':
      arguments->recursive = true;
      break;
    case 'k':
      if (arg) {
        arguments->nopenfd = argtoi(arg);
      }
      break;
    case ARGP_KEY_ARG:
      switch (state->arg_num) {
        case 0:
          arguments->regexp = arg;
          break;
        default:
          arguments->filenames.push_back(arg);
          //argp_usage(state);
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
"It provides only a *tiny* subset of grep features." ;
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


int process_file(const char* filename) {
  vector<rejit::Match> matches;
  struct stat file_stats;

  int fd = open(filename, O_RDONLY);
  if (fd < 0) {
    printf("jrep: %s: %s\n", filename, strerror(errno));
    return errno;
  }
  fstat(fd, &file_stats);
  size_t file_size = file_stats.st_size;
  char *file = (char*)mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);

  re->MatchAll(file, file_size, &matches);

  if (matches.size()) {
    // TODO: When not printing line numbers it may be faster to look for sos and
    // eos only for each match.
    vector<rejit::Match> new_lines;
    unsigned line_index = 0;
    rejit::MatchAll("^", file, file_size, &new_lines);
    // Append a match for the end of the file to be able to correctly print the
    // last line.
    new_lines.push_back({file + file_size, file + file_size});

    vector<rejit::Match>::iterator it;
    for (it = matches.begin(); it < matches.end(); it++) {
      rejit::Match match = (*it);
      if (arguments.print_filename) {
        printf("%s:", filename);
      }
      while (new_lines.at(line_index).begin <= match.begin) {
        ++line_index;
      }
      --line_index;
      if (arguments.print_line_number) {
        printf("%d:", line_index + 1);  // Index starts at 0, but line numbering at 1.
      }
#define START_RED "\x1B[31m"
#define END_COLOR "\x1B[0m"
      printf("%.*s" START_RED "%.*s" END_COLOR "%.*s",
             (int)(match.begin - new_lines.at(line_index).begin),
             new_lines.at(line_index).begin,
             (int)(match.end - match.begin),
             match.begin,
             (int)(new_lines.at(line_index + 1).begin - match.end),
             match.end);
    }
  }

  munmap(file, file_size);
  close(fd);

  return EXIT_SUCCESS;
}


int ftw_callback(const char *path, const struct stat *s, int typeflag) {
  // The regexp and arguments are global to easily be accessed via this callback.
  if (typeflag == FTW_F) {
    process_file(path);
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

  if (strcmp(arguments.regexp, "^") == 0 ||
      strcmp(arguments.regexp, "$") == 0) {
    printf("TODO: Handle '^' and '$'.");
    return EXIT_FAILURE;
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
