#!/usr/bin/python

import os
import sys
import subprocess

regexp="integer"

rejit_run='rejit/run'

path_run = os.path.dirname(os.path.realpath(__file__))
path_benchmarks = os.path.join(path_run, '../..')
path_testfiles = os.path.join(path_benchmarks, './resources/testfiles/')

run = os.path.join(path_benchmarks, rejit_run)

p_testfiles = subprocess.Popen(["ls", "-rS", path_testfiles], stdout=subprocess.PIPE)
testfiles = p_testfiles.communicate()[0].split()

for lf in testfiles:
  f = os.path.join(path_testfiles, lf)
  p1 = subprocess.Popen(["/bin/ls", "-l", f], stdout=subprocess.PIPE)
  p2 = subprocess.Popen(["awk", '{print $5}'], stdout=subprocess.PIPE, stdin=p1.stdout)
  f_size = p2.communicate()[0]
  p = subprocess.Popen([run, regexp, f, f_size, '100'], stdout=subprocess.PIPE)
  p.wait()
  if p.returncode != 0:
    print "Error"
    sys.exit()
  speed = p.communicate()[0].split()[0]
  print speed
