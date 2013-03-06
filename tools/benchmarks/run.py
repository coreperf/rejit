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

# Arguments handling -----------------------------------------------------------
import argparse
parser = argparse.ArgumentParser(description='Run rejit benchmarks')
parser.add_argument('--register',
    help="Register benchmarks results and plot performance over time.",
    action="store_true")
# TODO(rames): The html building should be part of another script.
parser.add_argument('--plot',
    help="Don't run the benchmark, only plot the results.",
    action="store_true")
args = parser.parse_args()

out_name = 'data.register' if args.register else 'data.temp'
if args.register:
  print("\nRegistering results to data files.\n")

# Import rejit utils.
dir_benchmarks = dirname(os.path.realpath(__file__))
dir_rejit = dir_benchmarks
while 'SConstruct' not in os.listdir(dir_rejit):
  dir_rejit = os.path.realpath(join(dir_rejit, '..'))
sys.path.insert(0, join(dir_rejit, 'tools'))
import utils
from utils import *

# Path helpers.
def benchmark_path(benchmark):
  return os.path.join(dir_benchmarks, benchmark)
def benchmark_description_path(benchmark):
  return os.path.join(benchmark_path(benchmark), 'description.html')
def benchmark_engine_path(benchmark, engine):
  return os.path.join(benchmark_path(benchmark), engine) 
def benchmark_engine_data_path(benchmark, engine):
  return os.path.join(benchmark_engine_path(benchmark, engine), out_name) 

# Build benchmarks in release mode ---------------------------------------------
print "\nBuilding benchmarks..."
subprocess.call(["scons", "-C", dir_rejit, join(utils.build_dir('release'), 'benchmark'), "benchtest=on"])

# Run the benchmarks -----------------------------------------------------------
def run_benchs():
  print "\nRunning benchmarks..."
  p_runs = subprocess.Popen(["find", dir_benchmarks, "-name", "run"], stdout=subprocess.PIPE)
  runs = p_runs.communicate()[0].split()

  for run in runs:
    time.sleep(1)
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


def plot_results():
  print "\nPlotting results"

  html_results = open(os.path.join(utils.dir_html, 'rejit.html'), 'w')
  html_header = open(os.path.join(utils.dir_html_resources, 'rejit.html.header'), 'r')
  html_footer = open(os.path.join(utils.dir_html_resources, 'rejit.html.footer'), 'r')

  html_results.write(html_header.read())

# Write to html helpers.
  indent_pattern = '  '
# We start at two level indentation in <html><body>.
  indent_level = 2


  def indent():
    global indent_level
    indent_level = indent_level + 1
  def unindent():
    global indent_level
    indent_level = indent_level - 1
  def html_unindented(string):
    html_results.write(string)
  def html(string):
    html_results.write(indent_pattern * indent_level + string)

  graphs_list = []

  def write_bench_data(benchmark):
    p_engines = subprocess.Popen(["ls", benchmark_path(benchmark)], stdout=subprocess.PIPE)
    engines = p_engines.communicate()[0].split()
    engines = filter(lambda x: isfile(benchmark_engine_data_path(benchmark, x)), engines)

    description_file = open(os.path.join(benchmark_path(benchmark), 'description.html'), 'r')
    description = description_file.read()
    description_file.close()

    html_dic = {
        'benchmark': benchmark,
        'plot_parallel': 'plot_parallel_' + benchmark,
        'description': description
        }
    html('''
    <tr><td>
    <div style="padding-left: 5em;">
      %(description)s
    </div>
    </td></tr>
    <tr><td>
    <div style="padding:32px">
      <div id="%(plot_parallel)s" style="width:600px;height:400px"></div>
    </div>
    <script type="text/javascript">

  $(function () {
  ''' % html_dic)

    series = {}

    for engine in engines:
      data_file = open(benchmark_engine_data_path(benchmark, engine), 'r')
      data = csv.reader(data_file, delimiter=' ')

      # Performance points have numerical indexes.
      first_perf_index = 0
      labels_all = data.next()
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

    for legend in sorted(series, reverse=True):
      data_points[legend] = ''
      for i in range(0, n_l):
        # TODO: find a beter fix for that
        if series[legend][i] != '' and series[legend][i] != 'inf':
          data_points[legend] += '[%s,%s],' %(labels[i], series[legend][i])
      html('var data_%s_%s = [%s];\n' %(benchmark, legend, data_points[legend]))

    html('''
      var plot_%(benchmark)s = $.plot($("#%(plot_parallel)s"),
         [
           ''' % html_dic)
    
    data_file.close()

    main_colors = ['#DEBD00', '#277AD9', '#00940A']
    secondary_colors = ['#E0D48D', '#94B8E0', '#72B377']
    colors_index = -1
    prev_root = ''
    for legend in sorted(series, reverse=True):
      l_s = legend.split('_')
      root = l_s[0]
      if root != prev_root:
        colors_index += 1
      set_info = '{data: data_%s_%s, label: "%s",\n' % (benchmark, legend,
        legend)
      if l_s[len(l_s) - 1] == 'worst' or l_s[len(l_s) - 1] == 'best':
        set_info += 'color: "%s",\n' % (secondary_colors[colors_index])
      else:
        set_info += 'color: "%s",\n' % (main_colors[colors_index])
      set_info += '},'
      html(set_info)
      prev_root = root
    html('''
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
    html('</td>')

    if args.register:
      # Plot benchmarks over time.
      # TODO
      data_points = {}
      if 'rejit' in engines:
        engine = 'rejit'
        data_file = open(benchmark_engine_data_path(benchmark, engine), 'r')
        data = csv.reader(data_file, delimiter=' ')
  
        # Performance points have numerical indexes.
        first_perf_index = 0
        labels = data.next()
        while not is_number(labels[first_perf_index]):
          first_perf_index += 1
        interest_label_index = first_perf_index + 8
        date_index = 1
        commit_index = 2
  
        for line in data:
          if line == []:
            continue
          # TODO: this does not guarantee the latest data.
          legend = ('%s_%s' %(engine, line[0])).rstrip(' \t\n\r').lstrip(' \t\n\r')
          if not legend in data_points:
            data_points[legend] = ''
          data_points[legend] += '[%s,%s,"%s"],' %(line[date_index], line[interest_label_index], line[commit_index])
  
        html_dic = {
            'id': benchmark + '_' + labels_all[interest_label_index]
            }
        html('''
        <td>
        <div style="padding:32px">
          <div id="%(id)s" style="width:600px;height:400px"></div>
        </div>
        <script type="text/javascript">
  
        $(function () {
        ''' % html_dic)
        for legend in data_points:
          html('var data_%s_%s_%s = [%s];\n' %(benchmark, legend, labels[interest_label_index], data_points[legend]))
  
        html('''
          var plot_%(id)s = $.plot($("#%(id)s"),
             [
               ''' % html_dic)
  
        for legend in data_points:
          html('{data: data_%s_%s_%s, label: "%s"},' %(benchmark, legend, labels[interest_label_index], legend))
  
      html('''
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
      ''' % html_dic)  # End of html


      html('</td>')

    #data_file.close()
    html('</tr>')

  # End of write_bench_data

  html('<table>')
  p_benchmarks = subprocess.Popen(["ls", dir_benchmarks], stdout=subprocess.PIPE)
  benchmarks = p_benchmarks.communicate()[0].split()
  benchmarks = filter(lambda x: isfile(benchmark_description_path(x)), benchmarks)
  for benchmark in benchmarks:
    write_bench_data(benchmark)

  html('</table>')
  html(html_footer.read())

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
