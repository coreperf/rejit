#!/usr/bin/python

# Copyright (C) 2013 Alexandre Rames <alexandre@uop.re>
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

parser = argparse.ArgumentParser(description='Run rejit tests.')
parser.add_argument('-j', '--jobs', default=1, type=int,
    help='Number of jobs to run simultaneously for the *build* commands')
parser.add_argument('--simd', choices=['on', 'off', 'both'], default='both',
    help='Test SIMD with the specified configurations.')
args = parser.parse_args()


dir_tests = dirname(os.path.realpath(__file__))
dir_rejit = dir_tests
while 'SConstruct' not in os.listdir(dir_rejit):
  dir_rejit = os.path.realpath(join(dir_rejit, '..'))
sys.path.insert(0, join(dir_rejit, 'tools'))
import utils


if args.simd == 'both':
  simd_modes = ['on', 'off']
else:
  simd_modes = [args.simd]

# Build and run tests in all modes.
# The automated tests test both with SIMD enabled and disabled for maximum
# coverage.
for simd_enabled in simd_modes:
  for mode in utils.build_options_modes:
    print "Testing (mode=%s, simd=%s)..." % (mode, simd_enabled)
    scons_command = ["scons", "-C", dir_rejit, '-j', str(args.jobs),
        "benchtest=on", "mode=%s" % mode, "simd=%s" % simd_enabled]
    subprocess.check_call(scons_command)
    print ''
    ptest = subprocess.Popen([join(utils.dir_build_latest, 'test-rejit')], stdout=subprocess.PIPE)
    test_output = ptest.communicate()[0]
    if test_output:
      print test_output
    else:
      if ptest.returncode != 0:
        print 'FAILED'
      else:
        print 'success'
