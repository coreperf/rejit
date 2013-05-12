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
      # TODO: Restore the '-Werror' option.
      #'CCFLAGS' : ['-std=c++11', '-Wall', '-pedantic', '-Werror'],
      'CCFLAGS' : ['-std=c++11', '-Wall', '-pedantic'],
      'CPPPATH' : map(lambda p: join(utils.dir_rejit, p), ['src/', 'include/'])
      },
#   'build_option:value' : {
#     'environment_key' : 'values to append'
#     },
    'mode:debug' : {
      'CCFLAGS' : ['-g', '-DDEBUG']
      },
    'mode:release' : {
      'CCFLAGS' : ['-O3']
      },
    'arch:x64' : {
      'CCFLAGS' : ['-DREJIT_TARGET_ARCH_X64']
      },
    'modifiable_flags:on' : {
      'CCFLAGS' : ['-DMOD_FLAGS']
      },
    'benchtest:on' : {
      'CCFLAGS' : ['-DBENCHTEST']
      },
    'simd:off' : {
      'CCFLAGS' : ['-DNO_SIMD']
      },
    }


# DefaultVariable have a default value that depends on elements not known when
# variables are first evaluated.
def modifiable_flags_handler(env):
  env['modifiable_flags'] = 'on' if 'mode' in env and env['mode'] == 'debug' else 'off'
def benchtest_handler(env):
  env['benchtest'] = 'on' if 'mode' in env and env['mode'] == 'debug' else 'off'

vars_default_handlers = {
    # variable_name    : [ 'default val', 'handler'                ]
    'modifiable_flags' : [ 'mode==debug', modifiable_flags_handler ],
    'benchtest'        : [ 'mode==debug', benchtest_handler        ]
    }

def DefaultVariable(name, help, allowed):
  default_value = vars_default_handlers[name][0]
  allowed.append(default_value)
  return EnumVariable(name, help, default_value, allowed)

# Allow build options to be specified in a file.
vars = Variables('build_config.py')
# Define command line build options.
vars.AddVariables(
    EnumVariable('mode', 'Build mode', 'release', allowed_values=utils.build_options_modes),
    # For now only the x64 architecture is a valid target.
    EnumVariable('arch', 'Target architecture', utils.GuessArchitecture(), allowed_values=utils.build_options_archs),
    EnumVariable('os', 'Target os', utils.GuessOS(), allowed_values=utils.build_options_oses),
    DefaultVariable('benchtest', 'Compile for benchmarks or tests.', ['on', 'off']),
    DefaultVariable('modifiable_flags', 'Allow modifying flags at runtime.', ['on', 'off']),
    # TODO: We stick with an enumerated variable to keep the 'on' 'off' scheme
    # and avoid introducing helpers to convert types when looking in the options
    # dictionary. But this may be refactored to use boolean variables if
    # cleaner.
    EnumVariable('simd', 'Allow SIMD usage', 'on', allowed_values=['on', 'off']),
    )
# To avoid recompiling multiple times when build options are changed, different
# build paths are used depending on the options set.
# This lists the options that should be taken into account to create the build
# path.
# TODO: Clean this. Create an option class.
options_influencing_build_path = [
#   ('option_name', include_option_name_in_path),
    ('mode', False),
    ('benchtest', True),
    ('modifiable_flags', True),
    ('simd', True)
    ]

# Construct the build environment ----------------------------------------------

env = Environment(variables = vars)

# Grab compilation environment variables.
env['CC'] = os.getenv('CC') or env['CC']
env['CXX'] = os.getenv('CXX') or env['CXX']
env['CCFLAGS'] = os.getenv('CCFLAGS') or env['CCFLAGS']
if os.getenv('LD_LIBRARY_PATH'):
  env['LIBPATH'] = os.getenv('LD_LIBRARY_PATH')
elif 'LIBPATH' not in env:
  env['LIBPATH'] = ''

Help(vars.GenerateHelpText(env))

# Abort build if any command line option is invalid.
unknown_build_options = vars.UnknownVariables()
if unknown_build_options:
  print 'Unknown build options:',  unknown_build_options.keys()
  Exit(1)

# This allows colors to be displayed when using with clang.
env['ENV']['TERM'] = os.environ['TERM']


# Process the build options.
# 'all' is unconditionally processed.
if 'all' in options:
  for var in options['all']:
    if var in env and env[var]:
      env[var] += options['all'][var]
    else:
      env[var] = options['all'][var]

# TODO: Clean this
# The benchmarks require modifiable flags. But this needs to be set before we
# compute the build dir path.
if 'benchmark' in COMMAND_LINE_TARGETS:
  env['modifiable_flags'] = 'on'
  env['benchtest'] = 'on'

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
      env[var] += options[key_val_couple][var]


# Sources and build targets ----------------------------------------------------
# Sources are in src/. Build in build/ to avoid spoiling the src/ directory with
# built objects.
build_dir = utils.dir_build
utils.ensure_dir(build_dir)
for option in options_influencing_build_path:
  if option[1]:
    build_dir = join(build_dir, option[0] + '_'+ env[option[0]])
  else:
    build_dir = join(build_dir, env[option[0]])
# Create a link to the latest build directory.
subprocess.check_call(["rm", "-f", utils.dir_build_latest])
subprocess.check_call(["ln", "-s", build_dir, utils.dir_build_latest])
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

env['LIBPATH'] += ':' + build_dir

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
    LIBS=[librejit])
env.Alias('basic', basic)
#TODO: Check for argp. Extract build to a SConscript?
jrep_libs = [librejit]
if env['os'] == 'macos':
  jrep_libs += ['argp']
jrep = env.Program(join(build_dir, 'jrep'), join(build_dir_sample, 'jrep.cc'),
    LIBS=jrep_libs)
env.Alias('jrep', jrep)
regexdna = env.Program(join(build_dir, 'regexdna'), join(build_dir_sample, 'regexdna.cc'),
    LIBS=[librejit])
env.Alias('regexdna', regexdna)
t_test = env.Program(join(build_dir, 'test-rejit'), join(build_dir_tests, 'test.cc'),
    LIBS=[librejit])
Default(basic, jrep, regexdna, t_test)

# Building benchmarks involve checking out and compiling third-party engines.
# We don't want to do that by default.
# Use a dummy top level target provided to trigger the compilation of all
# benchmark targets. This is achieved by conditionally including the SConscript
# below.
benchmark = join(build_dir, 'benchmark')
Program(benchmark, join(build_dir_tools, 'benchmarks/benchmark.c'))
Alias('benchmark', benchmark)
if benchmark in COMMAND_LINE_TARGETS or 'benchmark' in COMMAND_LINE_TARGETS:
  help_messages = utils.help_messages
  SConscript('tools/benchmarks/SConscript', exports='env librejit help_messages')
