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

#ifndef REJIT_FLAGS_H_
#define REJIT_FLAGS_H_

// This is a centralised definition of the flags used by rejit.
// This allows to easily use macros to operate on all the flags at once.
// Note that defined macros are required to take 3 arguments, but can ignore
// some of them (often the debug and release mode default values).
// See for example the DECLARE_FLAG macro.

#define REJIT_PRINT_FLAGS_LIST(M)                                              \
/* flag_name             , release , debug mode default value */               \
M( print_ff_elements     , false   , false )                                   \
/* Print the list of regexps that the codegen will generate code for. */       \
M( print_re_list         , false   , false )                                   \
/* Print the regexp tree after parsing. */                                     \
M( print_re_tree         , false   , false )                                   \
/* Print info about the state ring. */                                         \
M( print_state_ring_info , false   , false )                                   \
/* Display information about reduction of fast-forward elements. */            \
M( print_ff_reduce       , false   , false )                                   \

#define REJIT_FLAGS_LIST(M)                                                    \
/* flag_name             , release , debug mode default value */               \
/* Emit extra code for debugging purposes. */                                  \
M( emit_debug_code       , false   , true  )                                   \
/* Show the ff elements chosen by the fast forward mechanism. */               \
/* For kMatchAll, print every match when registered. */                        \
M( trace_match_all       , false   , false )                                   \
/* Trace repetitions handling at parse time. */                                \
M( trace_repetitions     , false   , false )                                   \
/* Use the fast-forwarding mechanisms. */                                      \
M( use_fast_forward      , true    , true  )                                   \
/* Fast-forward early to improve the scanning speed when no matches appear. */ \
M( use_fast_forward_early, true    , true  )                                   \
/* Use / trace reduction of fast-forward elements (substring extraction). */   \
M( use_ff_reduce         , true    , true  )                                   \
/* Use parser level optimizations. */                                          \
M( use_parser_opt        , true    , true  )                                   \
/* Dump generated code. */                                                     \
M( dump_code             , false   , false )                                   \
REJIT_PRINT_FLAGS_LIST(M)

// Declare all the flags.
#if defined(DEBUG) || defined(MOD_FLAGS)
#define DECLARE_FLAG(name, r, d) extern bool FLAG_##name;
#else
#define DECLARE_FLAG(name, release_def, x) \
  static const bool FLAG_##name = release_def;
#endif
REJIT_FLAGS_LIST(DECLARE_FLAG)
#undef DECLARE_FLAG

#ifdef MOD_FLAGS
#define SET_FLAG(name, val) FLAG_##name = val
#else
#define SET_FLAG(name, val)
#endif

// This removes early exits to allow benchmarking.
// If set to false, searching for matches with a NULL match results will
// bailout early. If set to true, code will actually search for
// matches but simply not update the (NULL) match results.
#ifdef BENCHTEST
#define FLAG_benchtest true
#else
#define FLAG_benchtest false
#endif

#endif  // REJIT_FLAGS_H_
