#!/usr/bin/python

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

import sys
import os
from os.path import join, dirname
import subprocess
import argparse

# Import rejit utils.
dir_tests = dirname(os.path.realpath(__file__))
dir_rejit = dir_tests
while 'SConstruct' not in os.listdir(dir_rejit):
  dir_rejit = os.path.realpath(join(dir_rejit, '..'))
sys.path.insert(0, join(dir_rejit, 'tools'))
import utils

parser = argparse.ArgumentParser(description='Run rejit tests.',
                                 formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument('-j', '--jobs', type=int, action='store',
                    default=1, 
                    help='Number of jobs to run simultaneously for the *build* commands')
parser.add_argument('--mode', choices=utils.build_options_modes + ['both'], action='store',
                    default='both',
                    help='Test with the specified build modes.')
parser.add_argument('--simd', choices=['on', 'off', 'both'], action='store',
                    default='both',
                    help='Test SIMD with the specified configurations.')
parser.add_argument('--use_fast_forward', action='store',
                    choices=['on', 'off', 'both'], default='both',
                    help='Test with the fast-forward mechanisms for the specified configurations.')
args = parser.parse_args()


dir_tests = dirname(os.path.realpath(__file__))
dir_rejit = dir_tests
while 'SConstruct' not in os.listdir(dir_rejit):
  dir_rejit = os.path.realpath(join(dir_rejit, '..'))
sys.path.insert(0, join(dir_rejit, 'tools'))
import utils


if args.mode == 'both':
  build_modes = utils.build_options_modes
else:
  build_modes = [args.mode]

if args.simd == 'both':
  simd_modes = ['on', 'off']
else:
  simd_modes = [args.simd]

if args.use_fast_forward == 'both':
  use_fast_forward_modes = ['on', 'off']
else:
  use_fast_forward_modes = [args.use_fast_forward]

# Build and run tests in all modes.
# The automated tests test both with SIMD enabled and disabled for maximum
# coverage.
testing_str = "Testing ("
testing_str += "mode={:<" + str(max(map(lambda s: len(s), build_modes))) + "}, " 
testing_str += "simd={:<" + str(max(map(lambda s: len(s), simd_modes)) )+ "}, " 
testing_str += "use_fast_forward={:<" + str(max(map(lambda s: len(s), use_fast_forward_modes))) + "}" 
testing_str += ")...\t"
for mode in build_modes:
  for simd_enabled in simd_modes:
    for ff_enabled in use_fast_forward_modes:
      print testing_str.format(mode, simd_enabled, ff_enabled),
      sys.stdout.flush()  # Flush early to tell the user something is running.
      scons_command = ["scons", "-C", dir_rejit, 'test-rejit', '-j', str(args.jobs),
          "benchtest=on", "modifiable_flags=on", "mode=%s" % mode, "simd=%s" % simd_enabled]
      pscons = subprocess.Popen(scons_command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
      scons_ret = pscons.wait()
      if scons_ret != 0:
        print 'FAILED'
        print 'Command:'
        print ' '.join(scons_command)
        print 'Output:'
        scons_output = pscons.communicate()[0]
        print scons_output
      else:
        if ff_enabled == 'on':
          use_fast_forward = 1
        else:
          use_fast_forward = 0
        ptest = subprocess.Popen([join(utils.dir_build_latest, 'test-rejit'), '--use_fast_forward=%d' % use_fast_forward], stdout=subprocess.PIPE)
        test_output = ''
        test_ret = ptest.poll()
        while test_ret is None:
          test_output += ptest.communicate()[0]
          test_ret = ptest.poll()
        if test_ret != 0:
          print 'FAILED'
          print 'Output:'
          print test_output
        else:
          if test_output:
            print test_output,
