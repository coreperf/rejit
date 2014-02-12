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
import argparse
import itertools
import subprocess

# Import rejit utils.
dir_tests = dirname(os.path.realpath(__file__))
dir_rejit = dir_tests
while 'SConstruct' not in os.listdir(dir_rejit):
  dir_rejit = os.path.realpath(join(dir_rejit, '..'))
sys.path.insert(0, join(dir_rejit, 'tools'))
import utils



script_description = \
'''
Run rejit tests.
The tests are run for a all possible combinations of options that are required
to be tested.
'''



def optionify(name):
  return '--' + name



# The options that can be tested are abstracted to provide an easy way to add
# new ones.
# Build options are options passed to scons, with a syntax like `scons opt=val`
# Run (short for runtime) options are options passed to the test program and
# control runtime flags in rejit.
# See the definition of `test_options` below.

# 'all' is a special value for the options. If specified, all other values of
# the option are tested.
class TestOption(object):
  def __init__(self, option_type, name, help,
               val_test_choices, val_test_default = None):
    self.name = name
    self.option_type = option_type
    self.help = help
    self.val_test_choices = val_test_choices
    if val_test_default is not None:
      self.val_test_default = val_test_default
    else:
      self.val_test_default = val_test_choices[0]

  type_build = 'build'
  type_run = 'run'

  def is_build_option(self):
    return self.option_type == TestOption.type_build
  def is_run_option(self):
    return self.option_type == TestOption.type_run

class BuildOption(TestOption):
  option_type = TestOption.type_build
  def __init__(self, name, help,
               val_test_choices, val_test_default = None):
    super(BuildOption, self).__init__(BuildOption.option_type, name, help, val_test_choices, val_test_default)
  def args_list(self, to_test):
    res = []
    if to_test == 'all':
      for val in self.val_test_choices:
        if val != 'all':
          res.append(self.name + '=' + val)
    else:
      res.append(to_test)
    return res

class RunOption(TestOption):
  option_type = TestOption.type_run
  def __init__(self, name, help,
               val_test_choices, val_test_default = None):
    super(RunOption, self).__init__(RunOption.option_type, name, help, val_test_choices, val_test_default)
  def args_list(self, to_test):
    res = []
    if to_test == 'all':
      for val in self.val_test_choices:
        if val != 'all':
          res.append(optionify(self.name) + '=' + val)
    else:
      res.append(to_test)
    return res



test_options = [
  BuildOption('mode', 'Test with the specified build modes.',
              val_test_choices=['all'] + utils.build_options_modes),
  BuildOption('simd', 'Test with the specified SIMD configurations.',
              val_test_choices=['all', 'on', 'off']),
  RunOption('use_fast_forward', 'Test with the specified configurations for fast-forwarding.',
            val_test_choices=['all', '1', '0']),
  RunOption('use_ff_reduce', 'Test with the specified configurations for common substrings extraction.',
            val_test_choices=['all', '1', '0'])
]

test_build_options = [opt for opt in test_options if opt.is_build_option()]
test_run_options = [opt for opt in test_options if opt.is_run_option()]



def ParserAddTestOptionsArguments(parser, test_opts):
  for opt in test_opts:
    parser.add_argument(optionify(opt.name),
                        choices=opt.val_test_choices,
                        default=opt.val_test_default,
                        help=opt.help,
                        action='store')


parser = argparse.ArgumentParser(description=script_description,
                                 formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument('-j', '--jobs', type=int, action='store',
                    default=1,
                    help='Number of jobs to run simultaneously for the *build* commands')
ParserAddTestOptionsArguments(parser, test_options)
args = parser.parse_args()


# List build and run options applicable for this test run.
test_build_args_list = map(lambda opt : opt.args_list(args.__dict__[opt.name]), test_build_options)
test_run_args_list = map(lambda opt : opt.args_list(args.__dict__[opt.name]), test_run_options)
# List all combinations of build and run options that will be tested.
test_build_args_combinations = list(itertools.product(*test_build_args_list))
test_run_args_combinations = list(itertools.product(*test_run_args_list))



options_format=''
for opt in test_options:
  options_format += optionify(opt.name) + '={:<' + str(max(map(len, opt.val_test_choices))) + '} '



for build_args in test_build_args_combinations:
  # Build the test executable for the current build options.
  scons_command = ["scons", "-C", dir_rejit, 'test-rejit',
                   '-j', str(args.jobs), "benchtest=on", "modifiable_flags=on"]
  scons_command += build_args
  print ' '.join(scons_command)
  p_scons = subprocess.Popen(scons_command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
  scons_ret = p_scons.wait()
  if scons_ret != 0:
    print 'Command:'
    print ' '.join(scons_command)
    print 'Output:'
    scons_output = p_scons.communicate()[0]
    print scons_output
    sys.exit(scons_ret)

  for run_args in test_run_args_combinations:
    test_command = [join(utils.dir_build_latest, 'test-rejit')]
    test_command += run_args
    print ' '.join(test_command),
    p_test = subprocess.Popen(test_command, stdout=subprocess.PIPE)
    test_output = ''
    test_ret = p_test.poll()
    while test_ret is None:
      test_output += p_test.communicate()[0]
      test_ret = p_test.poll()
    if test_ret != 0:
      print 'FAILED'
      print 'Output:'
      print test_output
    else:
      print '\t\tsuccess'
