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
import sys
import subprocess

from os.path import join


class RunResults:
  def __init__(self, regexp, engine):
    self.regexp = regexp
    self.engine = engine
    self.labels = []
    self.results = []

  def __repr__(self):
    res = ''
    for l in self.labels:
      res = res + l + ' '
    res = res.rstrip(' \n\t')
    res = res + '\n'
    for r in self.results:
      res = res + r + ' '
    res = res.rstrip(' \n\t')
    return res


def run_bench(regexp, engine, testfile, n_iterations, results):
  p1 = subprocess.Popen(["/bin/ls", "-l", testfile], stdout=subprocess.PIPE)
  p2 = subprocess.Popen(["awk", '{print $5}'], stdout=subprocess.PIPE, stdin=p1.stdout)
  testfile_size = p2.communicate()[0].split()[0].rstrip(' \n\t')
  p = subprocess.Popen([engine, regexp, testfile, testfile_size, n_iterations], stdout=subprocess.PIPE)
  p.wait()
  output = p.communicate()[0].split()

  results.labels.append(testfile_size)
  if p.returncode != 0:
    results.results.append('0')
  else:
    results.results.append(output[0])

def run_bench_files(regexp, engine, n_iterations, files):
  results = RunResults(regexp, engine)
  failed = False
  for f in files:
    p1 = subprocess.Popen(["/bin/ls", "-l", f], stdout=subprocess.PIPE)
    p2 = subprocess.Popen(["awk", '{print $5}'], stdout=subprocess.PIPE, stdin=p1.stdout)
    testfile_size = p2.communicate()[0].split()[0].rstrip(' \n\t')
    if not failed:
      p = subprocess.Popen([engine, regexp, f, testfile_size, n_iterations], stdout=subprocess.PIPE)
      p.wait()
      output = p.communicate()[0].split()

    results.labels.append(testfile_size)
    if p.returncode != 0 or failed:
      results.results.append('0')
      failed = True
    else:
      results.results.append(output[0])
  return results

