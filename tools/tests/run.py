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
args = parser.parse_args()

dir_tests = dirname(os.path.realpath(__file__))
dir_rejit = dir_tests
while 'SConstruct' not in os.listdir(dir_rejit):
  dir_rejit = os.path.realpath(join(dir_rejit, '..'))
sys.path.insert(0, join(dir_rejit, 'tools'))
import utils
import utils


# Build test executables in all modes.
for mode in utils.build_options_modes:
  print "Building tests (mode=%s)..." % mode
  subprocess.check_call(["scons", "-C", dir_rejit, '-j', str(args.jobs), "mode=%s" % mode, "benchtest=on"])
  print ''

# Run tests in all modes.
for mode in utils.build_options_modes:
  print "Running tests (mode=%s)..." % mode
  subprocess.call([join(dir_rejit, utils.build_dir(mode), 'test-rejit')])
  print ''

