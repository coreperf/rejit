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

# Arguments handling -----------------------------------------------------------
import argparse
parser = argparse.ArgumentParser(description='Run rejit benchmarks')
parser.add_argument('--register',
    help="Register benchmarks results and plot performance over time.",
    action="store_true")
parser.add_argument('--plot',
    help="Don't run the benchmark, only plot the results.",
    action="store_true")
args = parser.parse_args()

out_name = 'data.register' if args.register else 'data.temp'

# Path helpers.
def benchmark_path(benchmark):
  return join(dir_benchmarks, benchmark)
def benchmark_description_path(benchmark):
  return join(benchmark_path(benchmark), 'description.html')
def benchmark_engine_path(benchmark, engine):
  return join(benchmark_path(benchmark), engine) 
def benchmark_engine_data_path(benchmark, engine):
  return join(benchmark_engine_path(benchmark, engine), out_name) 

# Build benchmarks in release mode ---------------------------------------------
print "\nBuilding benchmarks..."
subprocess.call(["scons", "-C", dir_rejit, 'benchmark', "benchtest=on"])

# Run the benchmarks -----------------------------------------------------------
def run_benchs():
  print "\nRunning benchmarks..."
  p_runs = subprocess.Popen(["find", dir_benchmarks, "-name", "run"], stdout=subprocess.PIPE)
  runs = p_runs.communicate()[0].split()

  for run in runs:
    print "Running " + run
    p = subprocess.Popen([run], stdout=subprocess.PIPE)
    p.wait()
    out = p.communicate()[0]
    if not out:
      continue
    

    data_path = os.path.join(os.path.dirname(run), out_name)
    empty = not os.path.exists(data_path) or not os.path.isfile(data_path) or os.path.getsize(data_path) == 0
    if empty or not args.register:
      data = open(data_path, 'w+')
      data.write(out)
      data.close()
    else:
      data = open(data_path, 'r')
      labels = data.readline().rstrip(' \n\t')
      data.close()
      out_s = out.split('\n')
      current_labels = out_s[0].rstrip(' \t')
      if labels != current_labels:
        print "ERROR: labels line does not match in " + data_path
        print labels
        print current_labels
        print "exiting"
        sys.exit(1)
      else:
        data = open(data_path, 'a')
        for i in range(1, len(out_s)):
          data.write(out_s[i].rstrip(' \t'))
          data.write('\n')
        data.close()
# End of run_benchs()


# Plot the benchmarks results --------------------------------------------------

def html(html_file, string):
  html_file.write(string)

def write_benchmark_description(html_file, benchmark):
  description_file = open(join(benchmark_path(benchmark), 'description.html'), 'r')
  description = description_file.read()
  description_file.close()
  html(html_file,
  '''
  <tr>
    <td>
      <div style="padding-left: 5em;"> %(description)s </div>
    </td>
  </tr>
  ''' % { 'description': description }
     )

def write_benchmark_latest_results(html_file, engines, benchmark):
  html_dic = {
      'benchmark': benchmark,
      'plot_parallel': 'plot_parallel_' + benchmark,
      }
  html(html_file,
  '''
  <tr>
    <td>
      <div style="padding:32px">
        <div id="%(plot_parallel)s" style="width:600px;height:400px"></div>
      </div>
      <script type="text/javascript">
        $(function () {''' % html_dic)

  series = {}

  for engine in sorted(engines):
    data_file = open(benchmark_engine_data_path(benchmark, engine), 'r')
    data = csv.reader(data_file, delimiter=' ')

    # Performance points have numerical indexes.
    first_perf_index = 0
    labels_all = data.next()
    if '' in labels_all:
      labels_all.remove('')
    while not is_number(labels_all[first_perf_index]):
      first_perf_index += 1

    labels = labels_all[first_perf_index::]
    n_l = len(labels)
    data_points = {}
    for line in data:
      if line != []:
        # TODO: this does not guarantee the latest data.
        legend = ('%s_%s' %(engine, line[0])).rstrip(' \t\n\r').lstrip(' \t\n\r')
        series[legend] = line[first_perf_index::]

  for legend in sorted(series):
    data_points[legend] = ''
    for i in range(0, n_l):
      # TODO: find a beter fix for that
      if series[legend][i] != '' and series[legend][i] != 'inf':
        data_points[legend] += '[%s,%s],' %(labels[i], series[legend][i])
    html(html_file, 'var data_%s_%s = [%s];\n' %(benchmark, legend, data_points[legend]))

  html(html_file,
  '''
    var plot_%(benchmark)s = $.plot($("#%(plot_parallel)s"),
       [
         ''' % html_dic)
  
  data_file.close()

  main_colors = ['#DEBD00', '#277AD9', '#00940A', '#A22EBF']
  secondary_colors = ['#E0D48D', '#94B8E0', '#72B377', '#BF6CD4']
  colors_index = -1
  prev_root = ''
  for legend in sorted(series):
    l_s = legend.split('_')
    root = '_'.join(l_s[0:len(l_s) - 1])
    if root != prev_root:
      colors_index += 1
    set_info = '{data: data_%s_%s, label: "%s",\n' % (benchmark, legend,
      legend)
    if l_s[len(l_s) - 1] == 'worst' or l_s[len(l_s) - 1] == 'best':
      set_info += 'color: "%s",\n' % (secondary_colors[colors_index])
    else:
      set_info += 'color: "%s",\n' % (main_colors[colors_index])
    set_info += '},'
    html(html_file, set_info)
    prev_root = root
  html(html_file,
      '''
       ],
       plot_options_parallel
       );

    function showTooltip(x, y, color, contents) {
        $('<div id="tooltip">' + contents + '</div>').css( {
            position: 'absolute',
            display: 'none',
            top: y + 5,
            left: x + 5,
            border: '1px solid #fdd',
            padding: '2px',
            'background-color': color,
            opacity: 0.80
        }).appendTo("body").fadeIn(200);
    }

    var previousPoint = null;
    $("#%(plot_parallel)s").bind("plothover", function (event, pos, item) {
        $("#x").text(pos.x.toFixed(2));
        $("#y").text(pos.y.toFixed(2));

        if (item) {
            if (previousPoint != item.dataIndex) {
                previousPoint = item.dataIndex;
                
                $("#tooltip").remove();
                var x = item.datapoint[0].toFixed(2),
                    y = item.datapoint[1].toFixed(2);
                
                showTooltip(item.pageX, item.pageY,
                            item.series.color,
                            item.series.label + '<br/>' +
                            byteSF(y) + ' (' + Math.floor(y) + ' B/s)' + '<br/>' +
                            byteF(x)  + ' (' + Math.floor(x) + ' B)');

            }
        }
        else {
            $("#tooltip").remove();
            previousPoint = null;            
        }
    });

});
</script>
''' % html_dic)  # End of html
  html(html_file, '</td>')

def plot_over_time(html_file, engine, benchmark, interest_labels):
  data_file = open(benchmark_engine_data_path(benchmark, engine), 'r')
  csv_data = csv.reader(data_file, delimiter=' ')
  # Copy the csv data to a list, which is more convenient to work with.
  data = []
  for line in csv_data:
    data.append(line)
  data_file.close()

  labels = data[0]
  date_index = labels.index('date')
  commit_index = labels.index('commit')
  if not date_index or not commit_index:
    Error('Incorrectly formatted labels.')

  # We want the performance for the following file sizes.
  interest_labels = ['8', '2048', '1048576']
  interest_indexes = []
  for label in interest_labels:
    if labels.index(label):
      interest_indexes.append(labels.index(label)) 


  for label in interest_labels:
    data_points = {}
    # Create the data points from the data file.
    # Don't forget to skip the labels line.
    for line in data[1::]:
      if line == []:
        continue
      legend = ('%s_%s_%s' %(engine, label, line[0])).rstrip(' \t\n\r').lstrip(' \t\n\r')
      if not legend in data_points:
        data_points[legend] = ''
      data_points[legend] += '[%s,%s],' %(line[date_index], line[labels.index(label)])

    html_dic = { 'id': benchmark + '_' + labels[labels.index(label)] }

    html(html_file, '''
    <td>
      <div style="padding:32px"><div id="%(id)s" style="width:600px;height:400px"></div></div>
      <script type="text/javascript">
        $(function () {''' % html_dic
        )
    for legend in sorted(data_points):
      html(html_file, '''
          var data_%s_%s_%s = [%s];\n''' %(benchmark, legend, labels[labels.index(label)], data_points[legend])
          )

    html(html_file, '''
          var plot_%(id)s = $.plot($("#%(id)s"),
                                   [''' % html_dic
        )

    for legend in sorted(data_points):
      html(html_file, '''
                                     {data: data_%s_%s_%s, label: "%s"},''' %(benchmark, legend, labels[labels.index(label)], legend)
          )

    html(html_file, '''
                                   ],
                                   plot_options_speed_time
                                  );
          function showTooltip(x, y, color, contents) {
              $('<div id="tooltip">' + contents + '</div>').css( {
                  position: 'absolute',
                  display: 'none',
                  top: y + 5,
                  left: x + 5,
                  border: '1px solid #fdd',
                  padding: '2px',
                  'background-color': color,
                  opacity: 0.80
              }).appendTo("body").fadeIn(200);
          }

          var previousPoint = null;
          $("#%(id)s").bind("plothover", function (event, pos, item) {
              $("#x").text(pos.x.toFixed(2));
              $("#y").text(pos.y.toFixed(2));
    
              if (item) {
                  if (previousPoint != item.dataIndex) {
                      previousPoint = item.dataIndex;
                      
                      $("#tooltip").remove();
                      var x = item.datapoint[0].toFixed(2),
                          y = item.datapoint[1].toFixed(2);
                      
                      showTooltip(item.pageX, item.pageY,
                                  item.series.color,
                                  item.series.label + '<br/>' + x + '<br/>' + y);
    
                  }
              }
              else {
                  $("#tooltip").remove();
                  previousPoint = null;            
              }
          });

        });
      </script>
    </td>
    ''' % html_dic
        )


def plot_results():
  print "\nPlotting results"

  html_results = open(join(utils.dir_html, 'rejit.html'), 'w')
  html_header = open(join(utils.dir_html_resources, 'rejit.html.header'), 'r')
  html_footer = open(join(utils.dir_html_resources, 'rejit.html.footer'), 'r')

  html_results.write(html_header.read())

  graphs_list = []

  def write_benchmark_data(benchmark):

    p_engines = subprocess.Popen(["ls", benchmark_path(benchmark)], stdout=subprocess.PIPE)
    engines = p_engines.communicate()[0].split()
    engines = filter(lambda x: isfile(benchmark_engine_data_path(benchmark, x)), engines)

    write_benchmark_description(html_results, benchmark)

    write_benchmark_latest_results(html_results, engines, benchmark)

    if args.register and 'rejit' in engines:
      plot_over_time(html_results, 'rejit', benchmark, ['8', '2048'])

    html(html_results, '</tr>')

  # End of write_benchmark_data

  html(html_results, '<table>')
  p_benchmarks = subprocess.Popen(["ls", dir_benchmarks], stdout=subprocess.PIPE)
  benchmarks = p_benchmarks.communicate()[0].split()
  benchmarks = filter(lambda x: isfile(benchmark_description_path(x)), benchmarks)
  for benchmark in sorted(benchmarks):
    write_benchmark_data(benchmark)

  html(html_results, '</table>')
  html(html_results, html_footer.read())

  html_results.close()
  html_header.close()
  html_footer.close()
# End of plot_results()

# Main -------------------------------------------------------------------------
if args.plot:
  plot_results()
else:
  run_benchs()
  plot_results()
