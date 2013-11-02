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

import os
from os.path import join, abspath, dirname, isdir, isfile
import sys
import subprocess
import csv
import math
import time

# Import rejit utils.
dir_benchmarks = dirname(os.path.realpath(__file__))
dir_rejit = dir_benchmarks
while 'SConstruct' not in os.listdir(dir_rejit):
  dir_rejit = os.path.realpath(join(dir_rejit, '..'))
sys.path.insert(0, join(dir_rejit, 'tools'))
import utils
from utils import *



# Declare the arguments that will be built by the parser.
args = None

def verbose(message):
  if args.verbose:
    print(message)


# Engines ----------------------------------------------------------------------

BRE = 'BRE'
ERE = 'ERE'
RE_syntaxes = [BRE, ERE]


def default_commit_id():
  return '<unknown>'


class Engine:
  def __init__(self, name, exec_path, syntax,
               commit_id = default_commit_id):
    self.name = name

    self.exec_path = exec_path

    self.syntax = syntax
    if self.syntax not in RE_syntaxes:
      error("ERROR: Invalid syntax '%s'" % self.syntax)

    self.commit_id = commit_id

  def run(self, benchmark, sizes):
    if not os.path.exists(self.exec_path):
      error("Could not find: %s" % self.exec_path)

    if verbose:
      # The regexp is enclosed with quotes.
      printed_run_command = [
          self.exec_path,
          '"' + benchmark.regexp(self.syntax) + '"',
          '--iterations=' + str(args.iterations),
          '--size=' + ','.join(map(lambda x: str(x), sizes)),
          '--low_char=' + benchmark.low_char,
          '--high_char=' + benchmark.high_char
          ]
      verbose("Benchmarking %s for regexp \"%s\"" %(self.name, '"' + benchmark.regexp(self.syntax) + '"'))
      verbose("Command: %s" % (' '.join(printed_run_command)))

    run_command = [
        self.exec_path,
        benchmark.regexp(self.syntax),
        '--iterations=' + str(args.iterations),
        '--size=' + ','.join(map(lambda x: str(x), sizes)),
        '--low_char=' + benchmark.low_char,
        '--high_char=' + benchmark.high_char
        ]

    p = subprocess.Popen(run_command, stdout=subprocess.PIPE)
    rc = p.wait()
    if rc != 0:
      print("Failed to run:\n%s" % (' '.join(run_command)))
      print("Output:\n%s" % (p.communicate()[0]))
      error("Failed to run benchmark.")

    output = p.communicate()[0]
    if args.display:
      print output
    return output


engine_rejit = Engine('rejit', join(dir_benchmarks_engines, 'rejit/engine'), ERE,
                      utils.rejit_commit)
engine_re2   = Engine('re2',   join(dir_benchmarks_engines, 're2/engine'),   ERE,
                      utils.re2_commit)
engine_pcre  = Engine('pcre',   join(dir_benchmarks_engines, 'pcre/engine'),   ERE,
                      utils.pcre_commit)
engines = [engine_rejit, engine_re2, engine_pcre]
engines_names=map(lambda e: e.name, engines)



# Arguments handling -----------------------------------------------------------

import argparse

rejit_description = '''
Run rejit benchmarks.
Once run, you can find html graphs of the results in <rejit>/html/rejit.html.'''

parser = argparse.ArgumentParser(description=rejit_description)

parser.add_argument('--engines', action='store', nargs='+',
    choices=engines_names, default=engines_names,
    help='List of engines to benchmark.')
parser.add_argument('--sizes', type=int, action='store', nargs='+',
    default=map(lambda x: 1 << x, range(3, 24)),
    help='List of text sizes to benchmark.')
parser.add_argument('--slow_size_factor', action='store_true',
    default=3,
    help="For slow benchmarks, do not benchmark the <n> bigger text sizes.")
parser.add_argument('--iterations', type=int, action="store",
    default=100,
    help="Number of iterations to run benchmarks for.")
parser.add_argument('--nosimd', action='store_true',
    help='Disable SIMD usage.')
parser.add_argument('--nobuild', action='store_true',
    help="Do not build before running.")
parser.add_argument('-j', '--jobs', default=1, type=int,
    help='Number of jobs to run simultaneously for the *build* commands')
parser.add_argument('--display', action='store_true',
    help='Display benchmarks results as they execute.')
parser.add_argument('-v', '--verbose', action='store_true',
    help='Print extra information.')

args = parser.parse_args()

# Use the engines specified on the command line.
engines = [engine for engine in engines if engine.name in args.engines]

# Build benchmarks in release mode.
if not args.nobuild:
  print("Building benchmarks...")
  for engine in engines:
    build_command = ['scons', '-C', utils.dir_rejit, engine.name + '_engine', '-j', str(args.jobs), "benchtest=on"]
    if args.nosimd:
      scons_command += ['simd=off']
    utils.command_assert(build_command)






# Benchmarks -------------------------------------------------------------------

class ResultSet:
  def __init__(self, benchmark):
    self.benchmark = benchmark
    self.data = {}
    self.time = str(math.floor(time.time() * 1000))


  def add_result(self, engine, output):
    if engine.name in self.data:
      error("Results for engine %s already registered." % engine.name)
    data_engine = {}
    outs = output.split('\n')

    # The first line must be the labels.
    labels = outs[0].split()
    if not 'text_size' in labels:
      error("Expected labels line.")
    for label in labels:
      if label != "text_size":
        data_engine[label] = {}
    for raw_line in outs[1:]:
      line = raw_line.split()
      for i, val in enumerate(line[1:], start=1):
        # We want the dictionary to be indexed by integers for correct sorting.
        data_engine[labels[i]][int(line[0])] = val

    self.data[engine.name] = data_engine


  def plot_description(self):
    res = '''
  <tr>
    <td>
      <div style="padding-left: 5em;"> <code><pre>regexp: %s     range: ['%s','%s']</pre></code> </div>
    </td>''' % (self.benchmark.regexp(ERE), self.benchmark.low_char, self.benchmark.high_char)
    if self.benchmark.html_description:
      res += '''
    <td>
      <div style="padding-left: 5em;"> %s </div>
    </td>''' % self.benchmark.html_description
    res += '''
  </tr>
'''
    return res

  plot_colors = [('#DEBD00','#E0D48D'), ('#277AD9','#94B8E0'), ('#00940A','#72B377'), ('#A22EBF','#BF6CD4')]

  def plot_data(self):
    res = ''
    html_dic = {
        'bench_name': str(self.benchmark.bench_id),
        'graph_id': 'plot_parallel_' + str(self.benchmark.bench_id),
        'datasets_declaration': None,
        'datasets_definition': None,
        }

    def dataset_id(engine, label):
      return 'data_%s_%s_%s' % (self.benchmark.bench_id, engine, label)

    datasets_declaration = ''
    datasets_definition = ''

    for engine_index, engine in enumerate(self.data):
      data_engine = self.data[engine]
      for i, label in enumerate(data_engine):
        datapoints_string = ','.join(map(lambda x: '[%s,%s]' % (x, data_engine[label][x]), sorted(data_engine[label])))
        datasets_declaration += 'var %s = [%s];\n' % (dataset_id(engine, label), datapoints_string)
        datasets_definition += "{data: %(dataset_id)s, label: \"%(label)s\", color:\"%(color)s\"},\n" % {'dataset_id':dataset_id(engine, label), 'label':engine + ' ' + label, 'color':ResultSet.plot_colors[engine_index][0 if label == 'amortised' else 1]}

    html_dic['datasets_declaration'] = datasets_declaration
    html_dic['datasets_definition'] = datasets_definition


    res = '''
  <tr>
    <td>
      <div>
        <div id="%(graph_id)s" style="float:left;width:600px;height:400px"></div>
        <div style="float:left;"> <ul id="%(graph_id)s_choices" style="list-style: none;" class="flot_choices"> </ul> </div>
      </div>
      <script type="text/javascript">
        $(function () {
          %(datasets_declaration)s
          var datasets = [ %(datasets_definition)s ];

          var choiceContainer = $("#%(graph_id)s_choices");
          $.each(datasets, function(key, val) {
             choiceContainer.append('<li style="list-style: none;"><input type="checkbox" name="' + key +
                                    '" checked="checked" id="id' + key + '">' +
                                    '<label for="id' + key + '">'
                                    + val.label + '</label></li>');
          });

          plot_according_to_choices("%(graph_id)s", datasets, choiceContainer);
          $("#%(graph_id)s").bind("plothover", plothover_func);
          function replot() { plot_according_to_choices("%(graph_id)s", datasets, choiceContainer); }
          choiceContainer.find("input").change(replot);
          $('.legendColorBox > div').each(function(i){
                                          $(this).clone().prependTo(choiceContainer.find("li").eq(i));
                                          });
        });
      </script>
    </td>
''' % html_dic

    return res

  def plot(self):
    verbose("Plotting results for benchmark \"%s\" (range [%s,%s])" % (self.benchmark.regexp(ERE), self.benchmark.low_char, self.benchmark.high_char))
    res = ''
    res += self.plot_description()
    res += self.plot_data()

    return res

    for engine in engines:
      if not engine.name in self.data:
        error("Could not find benchmark results for engine %s for regexp \"%s\"." % (engine.name, self.benchmark.regexp(engine.syntax)))


results = []


class Benchmark:
  # Used to generate identifiers in the html code.
  bench_id = 1
  def __init__(self, regexp_BRE, regexp_ERE = None, low_char='0', high_char='z', html_description=None, sizes=args.sizes):
    self.bench_id = Benchmark.bench_id
    Benchmark.bench_id = Benchmark.bench_id + 1
    self.regexps = {}
    self.regexps[BRE] = regexp_BRE
    if regexp_ERE is None:
      self.regexps[ERE] = self.regexps[BRE]
    else:
      self.regexps[ERE] = regexp_ERE
    self.low_char = low_char
    self.high_char = high_char
    self.html_description = html_description
    self.sizes = sizes

  def list_regexps(self):
    print self.regexps

  def regexp(self, syntax):
    if not syntax in self.regexps:
      print("This benchmark does not provide a regexp for syntax '%s'" % syntax)
      list_regexps()
      error("Unavailable syntax.")
    return self.regexps[syntax]

  def run(self, engines):
    res = ResultSet(self)
    for engine in engines:
      res.add_result(engine, engine.run(self, self.sizes))
    return res



benchmarks = [
    Benchmark("abcdefgh", low_char='b', high_char='z'),
    Benchmark("abcdefgh"),
    Benchmark("abcdefgh", low_char='a', high_char='j'),
    Benchmark("([complex]|(regexp)){2,7}abcdefgh(at|the|[e-nd]as well)", sizes=args.sizes[:len(args.sizes) - args.slow_size_factor]),
    Benchmark("(12345678|abcdefghijkl)", sizes=args.sizes[:len(args.sizes) - args.slow_size_factor]),
    Benchmark("(12345678|xyz)", sizes=args.sizes[:len(args.sizes) - args.slow_size_factor]),
    Benchmark("(abcd--|abcd____)"),
    ]





def run_benchmarks():
  print("Running benchmarks...%s" % (" (Use `--verbose` and/or `--display` for more information)" if not args.verbose and not args.display else ""))
  if args.verbose:
    verbose('Engine versions:')
    for engine in engines:
      verbose('\t%s:\t%s' % (engine.name, engine.commit_id()))

  for bench in benchmarks:
    results.append(bench.run(engines))

def plot_results():
  utils.command_assert(['scons', '-C', utils.dir_rejit, 'flot_js'])

  print("Plotting results...")

  html_file_results = open(join(utils.dir_html, 'rejit.html'), 'w')

  html_file_header = open(join(utils.dir_html_resources, 'rejit.html.header'), 'r')
  html_file_results.write(html_file_header.read())
  html_file_header.close()

  html_file_results.write('<table style="text-align:right;">\n')
  html_file_results.write('<tr><td>engine</td><td style="padding-left:50px;">commit</td></tr>')
  for engine in engines:
    html_file_results.write('<tr>\n')
    html_file_results.write('  <td>%s</td><td style="padding-left:50px;"><pre style="padding:0 0 0 0;margin:0 0 0 0;">%s</pre></td>\n' % (engine.name, engine.commit_id()))
    html_file_results.write('</tr>\n')

  html_file_results.write('</table>\n')

  html_file_results.write('<table>\n')
  for res in results:
    html_file_results.write(res.plot())
  html_file_results.write('</table>\n')

  html_file_footer = open(join(utils.dir_html_resources, 'rejit.html.footer'), 'r')
  html_file_results.write(html_file_footer.read())
  html_file_footer.close()

  html_file_results.close()



run_benchmarks()
plot_results()
