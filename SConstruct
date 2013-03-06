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

import os
import sys

from os.path import join, dirname

dir_root = dirname(File('SConstruct').rfile().abspath)
sys.path.insert(0, join(dir_root, 'tools'))
import utils

#TODO(rames): Check scons AddOption()

Help("""
Simply building with
  $ scons
will build the rejit library in build/release.
See below for further options.
""")

# Build options ----------------------------------------------------------------

# Conveniently store all the options in a dictionary.
# The SConstruct will check the build variables and construct the build
# environment as appropriate.
options = {
    'all' : { # Unconditionally processed.
      'CCFLAGS' : '-std=c++11 -Wall -pedantic -Werror -Isrc/ -Iinclude/'
      },
#   'build_option:value' : {
#     'environment_key' : 'values to append'
#     },
    'mode:debug' : {
      'CCFLAGS' : '-g -DDEBUG'
      },
    'mode:release' : {
      'CCFLAGS' : '-O3'
      },
    'arch:x64' : {
      'CCFLAGS' : '-DREJIT_TARGET_ARCH_X64'
      },
    'modifiable_flags:on' : {
      'CCFLAGS' : '-DMOD_FLAGS'
      },
    'benchtest:on' : {
      'CCFLAGS' : '-DBENCHTEST'
      },
    }


# DefaultVariable have a default value that depends on elements not known when
# variables are first evaluated.
def mod_flags_handler(env):
  env['modifiable_flags'] = 'on' if 'mode' in env and env['mode'] == 'debug' else 'off'

vars_default_handlers = {
    # variable_name   : ['default val', 'handler'  ]
    'modifiable_flags': ['mode==debug', mod_flags_handler]
    }

def DefaultVariable(name, help, allowed):
  default_value = vars_default_handlers[name][0]
  allowed.append(default_value)
  return EnumVariable(name, help, default_value, allowed)

# Allow build options to be specified in a file.
vars = Variables('build_config.py')
# Define command line build options.
vars.AddVariables(
    EnumVariable('mode', 'build mode', 'release', allowed_values=utils.build_options_modes),
    # For now only the x64 architecture is a valid target.
    EnumVariable('arch', 'Target architecture', utils.GuessArchitecture(), allowed_values=utils.build_options_archs),
    EnumVariable('os', 'Target os', utils.GuessOS(), allowed_values=utils.build_options_oses),
    EnumVariable('benchtest', 'Compile for benchmarks or tests.', 'on', allowed_values=('on', 'off')),
    DefaultVariable('modifiable_flags', 'Allow modifying flags at runtime.', ['on', 'off']),
    )

# Construct the build environment ----------------------------------------------

env = Environment(variables = vars)

# Grab the compiler from the environment if it is available.
env["CC"] = os.getenv("CC") or env["CC"]
env["CXX"] = os.getenv("CXX") or env["CXX"]

Help(vars.GenerateHelpText(env))

# Abort build if any command line option is invalid.
unknown_build_options = vars.UnknownVariables()
if unknown_build_options:
  print "Unknown build options:",  unknown_build_options.keys()
  Exit(1)

# This allows colors to be displayed when using with clang.
env['ENV']['TERM'] = os.environ['TERM']

# Process the build options.
# 'all' is unconditionally processed.
if 'all' in options:
  for var in options['all']:
    if env[var]:
      env[var] += ' ' + options['all'][var]
    else:
      env[var] = options['all'][var]

# Other build options must match 'option:value'
dict = env.Dictionary()
keys = dict.keys()
keys.sort()
for key in keys:
  # First apply the default variables handlers.
  if key in vars_default_handlers and dict[key] == vars_default_handlers[key][0]:
    vars_default_handlers[key][1](dict)
  # Then update the environment according to the value of the variable.
  key_val_couple = key + ':%s' % dict[key]
  if key_val_couple in options:
    for var in options[key_val_couple]:
      env[var] += ' ' + options[key_val_couple][var]


# Sources and build targets ----------------------------------------------------
# Sources are in src/. Build in build/ to avoid spoiling the src/ directory with
# built objects.
build_dir = utils.build_dir(env['mode'])
def PrepareBuildDir(location):
  location_build_dir = join(build_dir, location)
  VariantDir(location_build_dir, location)
  return location_build_dir

PrepareBuildDir('.')
build_dir_src      = PrepareBuildDir('src')
build_dir_src_arch = join(PrepareBuildDir('src'), env['arch'])
build_dir_tools    = PrepareBuildDir('tools')
build_dir_tests    = join(build_dir_tools, 'tests')
build_dir_sample   = PrepareBuildDir('sample')

# For now it is easy to locate the sources. In the future we may require a
# system similar to the build options.
# All .cc files in src/ are source files.
# All .cc files in src/<arch> are source files.
# The src/platform/platform-<platform>.cc is required.
sources = [Glob(join(build_dir_src, '*.cc')),
    Glob(join(build_dir_src_arch, '*.cc')),
    join(build_dir_src, 'platform/platform-%s.cc' % env['os'])]

librejit = 'rejit'
env.StaticLibrary(join(build_dir, 'rejit'), sources)
basic = env.Program(join(build_dir, 'basic'), join(build_dir_sample, 'basic.cc'),
    LIBS=[librejit],  LIBPATH=build_dir, CPPPATH='include')
env.Alias('basic', basic)
t_test = env.Program(join(build_dir, 'test-rejit'), join(build_dir_tests, 'test.cc'),
    LIBS=[librejit],  LIBPATH=build_dir, CPPPATH='include')
Default(basic, t_test)

# Building benchmarks involve checking out and compiling third-party engines.
# We don't want to do that by default.
# Use a dummy top level target provided to trigger the compilation of all
# benchmark targets. This is achieved by conditionally including the SConscript
# below.
benchmark = join(build_dir, 'benchmark')
Program(benchmark, join(build_dir_tools, 'benchmarks/benchmark.c'))
Alias('benchmark', benchmark)
libpath = join(os.getcwd(), build_dir)
if benchmark or 'benchmark' in COMMAND_LINE_TARGETS:
  SConscript('tools/benchmarks/SConscript', exports='env librejit libpath')
