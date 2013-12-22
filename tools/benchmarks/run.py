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
import datetime

# Import rejit utils.
dir_benchmarks = dirname(os.path.realpath(__file__))
dir_rejit = dir_benchmarks
while 'SConstruct' not in os.listdir(dir_rejit):
  dir_rejit = os.path.realpath(join(dir_rejit, '..'))
sys.path.insert(0, join(dir_rejit, 'tools'))
import utils



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
               commit_id = default_commit_id,
               args_list_separator=',',
               args_list_assign_char='='):
    self.name = name

    self.exec_path = exec_path

    self.syntax = syntax
    if self.syntax not in RE_syntaxes:
      utils.error("ERROR: Invalid syntax '%s'" % self.syntax)

    self.commit_id = commit_id
    self.args_list_separator = args_list_separator
    self.args_list_assign_char = args_list_assign_char

  def run(self, benchmark, sizes):
    if not os.path.exists(self.exec_path):
      utils.error("Could not find: %s" % self.exec_path)

    run_command = [
        self.exec_path,
        benchmark.regexp(self.syntax),
        '--iterations=' + str(args.iterations),
        '--low_char=' + benchmark.low_char,
        '--high_char=' + benchmark.high_char,
        ] + ('--size' + self.args_list_assign_char + self.args_list_separator.join(map(str, sizes))).split(' ')
    # The regexp is enclosed with quotes.
    printed_run_command = [
        self.exec_path,
        '"' + benchmark.regexp(self.syntax) + '"',
        '--iterations=' + str(args.iterations),
        '--low_char=' + benchmark.low_char,
        '--high_char=' + benchmark.high_char,
        '--size' + self.args_list_assign_char + self.args_list_separator.join(map(str, sizes))
        ]

    if verbose or args.display:
      print("Benchmarking %s for regexp \"%s\"" %(self.name, '"' + benchmark.regexp(self.syntax) + '"'))
    if verbose:
      verbose("Command: %s" % (' '.join(printed_run_command)))


    p = subprocess.Popen(run_command, stdout=subprocess.PIPE)
    rc = p.wait()
    if rc != 0:
      print("Failed to run:\n%s" % (' '.join(printed_run_command)))
      print("Output:\n%s" % (p.communicate()[0]))
      utils.warning("Failed to run benchmark.")
      return None

    output = p.communicate()[0]
    if args.display:
      print output
    return output


engine_rejit = Engine('rejit', join(utils.dir_benchmarks_engines, 'rejit/engine'), ERE,
                      utils.rejit_commit)
engine_re2 = Engine('re2', join(utils.dir_benchmarks_engines, 're2/engine'), ERE,
                    utils.re2_commit)
engine_pcre = Engine('pcre', join(utils.dir_benchmarks_engines, 'pcre/engine'), ERE,
                     utils.pcre_commit)
engine_v8 = Engine('v8', join(utils.dir_benchmarks_engines, 'v8/engine'), ERE,
                     utils.v8_commit, args_list_separator=' ', args_list_assign_char=' ')
engines = [engine_rejit, engine_re2, engine_pcre, engine_v8]
engines_names=map(lambda e: e.name, engines)



# Arguments handling -----------------------------------------------------------

import argparse

rejit_description = '''
Run rejit benchmarks.
Once run, you can find html graphs of the results in <rejit>/html/rejit.html.'''

parser = argparse.ArgumentParser(description=rejit_description,
                                 formatter_class=argparse.ArgumentDefaultsHelpFormatter)

parser.add_argument('--engines', action='store', nargs='+',
                    choices=engines_names, default=engines_names,
                    help='Space-separated list of engines to benchmark.')
parser.add_argument('--sizes', type=int, action='store', nargs='+',
                    default=map(lambda x: 1 << x, range(3, 22)),
                    help='Space-separated list of text sizes to benchmark.')
parser.add_argument('--iterations', type=int, action="store",
                    default=100,
                    help="Number of iterations to run benchmarks for.")
parser.add_argument('--nosimd', action='store_true',
                    help='Disable SIMD usage.')
parser.add_argument('--nobuild', action='store_true',
                    help="Do not build before running.")
parser.add_argument('-j', '--jobs', type=int,
                    default=1, 
                    help='Number of jobs to run simultaneously for the *build* commands')
parser.add_argument('--display', action='store_true',
                    help='Display benchmarks results as they execute.')
parser.add_argument('--machine_description', action='store',
                    default=join(dir_benchmarks, 'machine_desc'),
                    help='Path to an html file describing the machine running'
                         'the benchmarks. The description is embedded in the'
                         'html results file.')
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
      utils.error("Results for engine %s already registered." % engine.name)
    data_engine = {}
    outs = output.split('\n')

    # The first line must be the labels.
    labels = outs[0].split()
    if not 'text_size' in labels:
      utils.error("Expected labels line.")
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
        utils.error("Could not find benchmark results for engine %s for regexp \"%s\"." % (engine.name, self.benchmark.regexp(engine.syntax)))


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
      utils.error("Unavailable syntax.")
    return self.regexps[syntax]

  def run(self, engines):
    res = ResultSet(self)
    for engine in engines:
      output = engine.run(self, self.sizes)
      if output is not None:
        res.add_result(engine, output)
    return res



benchmarks = [
    Benchmark("abcdefgh", low_char='b', high_char='z'),
    Benchmark("abcdefgh"),
    Benchmark("abcdefgh", low_char='a', high_char='j'),
    Benchmark("([complex]|(regexp)){2,7}abcdefgh(at|the|[e-nd]as well)"),
    Benchmark("(alternation|strings)"),
    Benchmark("(alternation|more|than|two|different|strings)"),
    Benchmark("(rather_long_string|min)"),
    Benchmark("(prefix abcd|prefix 1234)"),
    Benchmark("(abcd suffix|1234 suffix)"),
    Benchmark("(abcdefgh anywhere xyz|01 anywhere 56789)"),
    Benchmark("(some|[stuff])((other|regexps)? bla root blah | (abcdefgh boot{3,3} xyz | 00 foot 5678))"),
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

  html_file_results.write('<h2>Info</h2>')
  html_file_results.write('%s\n' % datetime.datetime.now().strftime("%Y/%m/%d %H:%M"))
  if args.machine_description:
    if not os.path.isfile(args.machine_description):
      utils.warning("Could not open '%s'" % args.machine_description)
    else:
      desc_file = open(args.machine_description, 'r')
      html_file_results.write('<h3>Machine description</h3>')
      html_file_results.write(desc_file.read())
      desc_file.close()

  html_file_results.write('<h3>Engines versions</h3>')
  html_file_results.write('<table style="text-align:right;">\n')
  html_file_results.write('<tr><td>engine</td><td style="padding-left:50px;">commit</td></tr>')
  for engine in engines:
    html_file_results.write('<tr>\n')
    html_file_results.write('  <td>%s</td><td style="padding-left:50px;"><pre style="padding:0 0 0 0;margin:0 0 0 0;">%s</pre></td>\n' % (engine.name, engine.commit_id()))
    html_file_results.write('</tr>\n')

  html_file_results.write('</table>\n')

  html_file_results.write('<h2>Results</h2>')
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
