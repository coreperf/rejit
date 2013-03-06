// Copyright (C) 2013 Alexandre Rames <alexandre@uop.re>
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

// TODO(rames): Optimize flags for release mode.
#ifdef MOD_FLAGS
#define FLAG(name, release_def, debug_def) extern bool FLAG_##name;
#else
#define FLAG(name, release_def, debug_def) \
  static const bool FLAG_##name = release_def;
#endif

#ifdef MOD_FLAGS
#define SET_FLAG(name, val) FLAG_##name = val
#else
#define SET_FLAG(name, val)
#endif

// This removes early exits to allow benchmarking.
// If set to false, searching for matches with a NULL match results will
// bailout early. If set to true, code will actually search for
// matches but simply not update the (NULL) match results.
extern bool FLAG_benchtest;

// Emit extra code for debugging purposes.
FLAG( emit_debug_code       ,false ,true  )

FLAG( use_fast_forward      ,true  ,true  )
FLAG( trace_ff_finder       ,false ,false )
// Disabled for poor performances.
FLAG( force_ff_pre_scan     ,false ,false )

FLAG( trace_repetitions     ,false ,false )
FLAG( trace_re_tree         ,false ,false )
FLAG( trace_re_list         ,false ,false )
FLAG( trace_matches         ,false ,true )
FLAG( print_state_ring_info ,false ,false )

#undef FLAG

#endif  // REJIT_FLAGS_H_
