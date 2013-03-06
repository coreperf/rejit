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

#include "flags.h"

#if defined(DEBUG) || defined(ALLOCATION_FLAG_MODIFICATION)
#define FLAG(name, release_def, debug_def) bool FLAG_##name = debug_def;
#else
#define FLAG(name, release_def, debug_def)
#endif

#ifdef BENCHTEST
bool FLAG_benchtest = true;
#else
bool FLAG_benchtest = false;
#endif

FLAG( emit_debug_code       ,false ,true  )

FLAG( use_fast_forward      ,true  ,true  )
FLAG( trace_ff_finder       ,false ,false )
FLAG( force_ff_pre_scan     ,false ,false )

FLAG( trace_repetitions     ,false ,false )
FLAG( trace_re_tree         ,false ,false )
FLAG( trace_re_list         ,false ,false )
FLAG( trace_matches         ,false ,false )
FLAG( print_state_ring_info ,false ,false )

#undef FLAG
