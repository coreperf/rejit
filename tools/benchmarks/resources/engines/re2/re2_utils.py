# Copyright (C) 2013 Alexandre Rames <alexandre@coreperf.com>

# rejit is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import os
from os.path import join, dirname, abspath, realpath
import sys
import subprocess
import math
import time
import gc

scriptpath = dirname(realpath(__file__))
dir_tools = join(scriptpath, '../../../../')
sys.path.insert(0, dir_tools)
import utils
from utils import *

def run(engine_args):
  engine = os.path.join(dir_benchmarks_engines, 're2/engine')

  current_time = str(math.floor(time.time() * 1000))
  current_commit = current_re2_commit()
  labels      = ['label'     , 'date'       , 'commit']
  m_worst     = ['worst'     , current_time , current_commit]
  m_amortised = ['amortised' , current_time , current_commit]
  m_best      = ['best'      , current_time , current_commit]
  results     = [labels, m_worst, m_amortised, m_best]

  for size in utils.default_run_sizes:
    args = [engine,
        '--iterations=' + str(utils.default_n_iterations),
        '--size=' + str(size)] + engine_args
    gc.collect()
    p = subprocess.Popen(args, stdout=subprocess.PIPE)
    ret = p.wait()
    if ret != 0:
      out_s = ['0', '0', '0']
    else:
      out = p.communicate()[0]
      out_s = out.split()

    labels.append(size)
    m_worst.append(out_s[0])
    m_amortised.append(out_s[1])
    m_best.append(out_s[2])

  m_worst     = map(lambda x: x if is_number(x) else '0', m_worst)
  m_amortised = map(lambda x: x if is_number(x) else '0', m_amortised)
  m_best      = map(lambda x: x if is_number(x) else '0', m_best)

  return results
