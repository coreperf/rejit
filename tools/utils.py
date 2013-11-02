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

import platform
import sys
import os
import subprocess

from os.path import join


def error_message(msg):
  print "ERROR: %s" % msg

def error(msg, rc = 1):
  error_message(msg)
  sys.exit(rc)

def check_rc(rc, message):
  if rc != 0:
    error(message, rc)


# Path helpers -----------------------------------------------------------------
dir_tools = os.path.dirname(os.path.realpath(__file__))
dir_root  = os.path.abspath(join(dir_tools, '..'))
dir_rejit  = os.path.realpath(dir_root)
dir_src                    = join(dir_root, 'src')
dir_html                   = join(dir_root, 'html')
dir_html_resources         = join(dir_html, 'resources')
dir_benchmarks             = join(dir_tools, 'benchmarks')
dir_benchmarks_resources   = join(dir_benchmarks, 'resources')
dir_benchmarks_engines     = join(dir_benchmarks_resources, 'engines')
dir_build                  = join(dir_root, 'build')
dir_build_latest           = join(dir_root, 'build', 'latest')


def ensure_dir(path_name):
  if not os.path.exists(path_name):
    os.makedirs(path_name)


# Build helpers ----------------------------------------------------------------
build_options_modes = ['release', 'debug']

build_options_archs = ['x64']
def GuessArchitecture():
  id = platform.machine()
  if id == 'x86_64':
    return 'x64'
  else:
    return None

build_options_oses = ['macos', 'linux']
def GuessOS():
  id = platform.system()
  if id == 'Linux':
    return 'linux'
  elif id == 'Darwin':
    return 'macos'
  else:
    return None

def assert_available(command):
  p = subprocess.Popen(['which', command], stdout=subprocess.PIPE)
  rc = p.wait()
  if rc != 0:
    "Command not available: %s" % command
    sys.exit(rc)

# TODO: Find out how to pass on 'cwd' instead of 'cwd_'
def command_assert(command, failure_message = None, cwd_ = None):
  p = subprocess.Popen(command, stdout=subprocess.PIPE, cwd=cwd_)
  for line in p.stdout.readlines():
    print line,
  rc = p.wait()
  if rc != 0:
    if failure_message:
      error_message(failure_message, rc)
    error("Failed command: %s" % ' '.join(command))



# Benchmarks helpers -----------------------------------------------------------

def rejit_commit():
  return subprocess.check_output(['git', 'rev-parse', 'HEAD']).strip('\n')

def re2_commit():
  return subprocess.check_output(['hg', 'id'], cwd=join(dir_benchmarks_engines, 're2', 'hg.re2')).strip('\n')

def pcre_commit():
  p1 = subprocess.Popen(['svn', 'info'], cwd=join(dir_benchmarks_engines, 'pcre', 'svn.pcre'), stdout=subprocess.PIPE)
  p2 = subprocess.Popen(['grep', '-i', 'revision'], stdout=subprocess.PIPE, stdin=p1.stdout)
  p3 = subprocess.Popen(['awk', '{print $2}'], stdout=subprocess.PIPE, stdin=p2.stdout)
  return p3.communicate()[0].rstrip(' \n\t')



# Help messages ----------------------------------------------------------------

help_argp = \
'''
Could not find libargp.
Please install libargp; this is the only extra step on OSX.
You can install it via homebrew or macports, or manually install it from
https://github.com/fizx/libbow-osx/tree/master/argp.
If you install it outside of the standard lib and include path, please compile
rejit as follow:
  $ export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:<path to libargp.a>
  $ export CCFLAGS=$CCFLAGS:-I<path to argp.h>
  $ scons benchmark
'''

help_messages = {
    'argp': help_argp
    }
