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
from os.path import join
import subprocess

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

def error(message, rc = 1):
  print 'ERROR: ' + message
  sys.exit(rc)



# Misc Helpers -----------------------------------------------------------------
def is_number(s):
  try:
    float(s)
    return True
  except ValueError:
    return False


def current_rejit_commit():
  p1 = subprocess.Popen(['git', 'log', '-n1'], stdout=subprocess.PIPE)
  p2 = subprocess.Popen(["head", '-n1'], stdout=subprocess.PIPE, stdin=p1.stdout)
  p3 = subprocess.Popen(["awk", '{print $2}'], stdout=subprocess.PIPE, stdin=p2.stdout)
  p3.wait()
  return p3.communicate()[0].rstrip(' \n\t')

def current_re2_commit():
  p1 = subprocess.Popen(['hg', 'head'], cwd=join(dir_benchmarks_engines, 're2', 'hg.re2'), stdout=subprocess.PIPE)
  p2 = subprocess.Popen(["grep", 'changeset'], stdout=subprocess.PIPE, stdin=p1.stdout)
  p3 = subprocess.Popen(["awk", '{print $2}'], stdout=subprocess.PIPE, stdin=p2.stdout)
  p3.wait()
  return p3.communicate()[0].rstrip(' \n\t')

def current_v8_commit():
  p1 = subprocess.Popen(['git', 'log', '-n1'], cwd=join(dir_benchmarks_engines, 'v8', 'git.v8'), stdout=subprocess.PIPE)
  p2 = subprocess.Popen(["grep", 'changeset'], stdout=subprocess.PIPE, stdin=p1.stdout)
  p3 = subprocess.Popen(["awk", '{print $2}'], stdout=subprocess.PIPE, stdin=p2.stdout)
  p3.wait()
  return p3.communicate()[0].rstrip(' \n\t')

def current_pcre_commit():
  p1 = subprocess.Popen(['svn', 'info'], cwd=join(dir_benchmarks_engines, 'pcre', 'svn.pcre'), stdout=subprocess.PIPE)
  p2 = subprocess.Popen(["grep", 'revision'], stdout=subprocess.PIPE, stdin=p1.stdout)
  p3 = subprocess.Popen(["awk", '{print $2}'], stdout=subprocess.PIPE, stdin=p2.stdout)
  p3.wait()
  return p3.communicate()[0].rstrip(' \n\t')

def Error(message):
  print message
  sys.exit(1)


# Benchmarks helpers -----------------------------------------------------------
# From 8B to 16MiB.
default_run_sizes = map(lambda x: 1 << x, range(3, 22))
# Number of iterations to run benchmarks for by default.
default_n_iterations = 100
# For v8 and small text sizes, multiply the number of iterations by a factor to
# obtain a decent average.
max_small_size = 128 * 1024
small_sizes_iteration_factor = 200



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
