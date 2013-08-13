Rejit is a prototype regular expression JIT compiler. It currently only supports
the x86_64 architecture.

For more detailed information about rejit, more benchmarks and some explanation of its mechanisms please read [this article][coreperf rejit].

For questions, feedback, suggestions, to talk about regular expression matching, optimisation, or other related topics, please use the [rejit-users][1] Google group or email me at <alexandre@coreperf.com> !

## Benchmarks

The results below were produced on a machine with a quad-core Intel Core
i5-2400 CPU @ 3.10GHz with 4GiB RAM on Fedora 17 (3.8.13-100.fc17.x86\_64).
It supports SSE4.2.

Results are reported for the following engines versions:

```
GNU grep version 2.14, commit: 599f2b15bc152cdf19022c7803f9a5f336c25e65
Rejit commit: b29ea4af1a3ae86dcb25bf961bc716029430c9b1
V8 version 3.20.9, commit: 455bb9c2ab8af080aa15e0fbf4838731f45241e8
Re2 commit: aa957b5e3374
```

#### Grepping recursively through the Linux kernel sources.

```
$ CMD='grep -R regexp linux-3.10.6/'; $CMD > /dev/null && time $CMD > /dev/null
real  0m0.622s
user  0m0.356s
sys   0m0.260s
```

 ```jrep``` is a grep-like utility powered by rejit.

```
$ CMD='jrep -R regexp linux-3.10.6/'; $CMD > /dev/null && time $CMD > /dev/null
real  0m0.370s
user  0m0.101s
sys   0m0.263s
```

The `jrep` utility performs 1.68 times faster than gnu grep in this very real
use-case!  The time spent in `sys` is equivalent, but Rejit spends 3 times less
time in `user` code.
<br />It is part of the sample programs in the rejit repository (see the
wiki).  It is of course far behind grep in terms of features, but
supports searching for multi-lines patterns and has initial multithreading
support.

#### DNA matching benchmark.

From the "[Computer Language Benchmarks Game][2]", this benchmark performs some DNA matching operations using regular expressions.

The tables below show performance (`real` running time) for different input sizes, for

the
[fastest registered single threaded implementation][cpu_bench single threaded fastest]
(V8) and a single-threaded Rejit-powered implementation.

```
input size                            V8      Rejit
    50.000 (500KB)                0.034s     0.015s
   500.000 (  5MB)                0.217s     0.130s
 5.000.000 ( 50MB)                2.054s     1.246s
50.000.000 (500MB)       (out of memory)    14.624s
```

the
[second fastest registered single threaded implementation][cpu_bench multi threaded fastest]
(Re2) and a multi-threaded Rejit-powered implementation. (A quick go at running
the first listed implementation would raise failures.)

```
input size                           Re2      Rejit
    50.000 (500KB)                0.022s     0.011s
   500.000 (  5MB)                0.183s     0.087s
 5.000.000 ( 50MB)                1.629s     0.971s
50.000.000 (500MB)               20.693s    11.594s
```

See performance for various engines and languages for [single-core][4] and [quad-core][5] implementations.
The rejit programs used to run these benchmarks are also part of the rejit
sample programs (see the wiki).

#### Complex regular expression matching
This is an example taken from rejit's benchmarks suite. It shows the performance to find all left-most longest matches of the regular expression ```([complex]|(regexp)){2,7}abcdefgh(at|the|[e-nd]as well)``` in randomly generated texts of various sizes. The performance reported is ```(<size of text> / <time to match>)```, which is easier to report and understand than close to zero time intervals.

It illustrates the 'fast forward' mechanism used by rejit (see [this article][coreperf rejit] for details).<br />

![Graph comparing performance of re2, v8, and rejit to match a complex regular expression][6]

### Benchmarks suite

Run the benchmarks and open the html file containing the results along with some comments:
```
$ ./tools/benchmarks/run.py
$ <browser> ./html/rejit.html
```
See the Sample programs section below for regexdna and jrep.

### Utilities

You can build the benchmark utilities to experiment more with performance:
```
$ scons benchmark
```

You will then find the benchmarking executables
```
tools/benchmarks/resources/engines/re2/engine
tools/benchmarks/resources/engines/rejit/engine
tools/benchmarks/resources/engines/v8/engine
```
which you can use to report processing speeds (bytes/s) for a regular expression
```
$ tools/benchmarks/resources/engines/rejit/engine "regexp"
3.41191e+09 (worst speed)
1.33176e+10 (amortised speed)
1.33856e+10 (best speed)
```
Use the ```--help``` option for details.

## Bulding and usage

All scripts and utilities should include a help message available via ```$ <script> --help```

Include ```<rejit>/include/rejit.h``` in your program.

```
#include <rejit.h>
using namespace std;
using namespace rejit;

int main() {
  string text = "";
  vector<Match> matches;
  rejit::MatchAll("regexp", text, &matches);
  for (vector<Match>::iterator it = matches.begin(); it < matches.end(); it++) {
    // Do something.
  }
  return 0;
}
```

Then build rejit
```
$ scons
```

and compile and link your program with the built library
```
$ g++ -o myprg myprg.cc -I<rejit>/include -L<rejit>/build/latest -lrejit
```

Documentation for the various functions offered by rejit are available as comments in ```include/rejit.h```.
You can also find examples in the sample programs provided in ```sample/```.

## Sample programs

A few sample programs using rejit are included in the ```sample/``` folder.
It includes the ```regexdna``` and ```jrep``` samples which were introduced in the benchmarks section above.
Compile them with:
```
$ scons sample/basic
$ scons sample/jrep
$ scons sample/regexdna
$ scons sample/regexdna-multithread
```

Use ```$ sample/<sample> --help``` for details.

## Misc

You can run the sparse rejit development test suite with ```$ ./tools/tests/run.py```.

Rejit is provided under the GPLv3 licence. For other licences contact me.


  [1]: https://groups.google.com/forum/?fromgroups#!forum/rejit-users
  [2]: http://benchmarksgame.alioth.debian.org/
  [3]: http://benchmarksgame.alioth.debian.org/u64/program.php?test=regexdna&lang=v8&id=2
  [4]: http://benchmarksgame.alioth.debian.org/u64/benchmark.php?test=regexdna&lang=all&data=u64
  [5]: http://benchmarksgame.alioth.debian.org/u64q/benchmark.php?test=regexdna&lang=all&data=u64q
  [6]: tools/benchmarks/resources/sample_bench_complex.png
  [cpu_bench single threaded fastest]: http://benchmarksgame.alioth.debian.org/u64/program.php?test=regexdna&lang=v8&id=2
  [cpu_bench multi threaded fastest]: http://benchmarksgame.alioth.debian.org/u32q/program.php?test=regexdna&lang=gpp&id=2
  [coreperf rejit]: http://coreperf.com/projects/rejit/
