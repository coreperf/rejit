Rejit is a prototype regular expression JIT compiler. It currently only supports
the x86_64 architecture.

For more detailed information about rejit, more benchmarks and some explanation of its mechanisms please see (TODO(write and link to article)).

For questions, feedback, suggestions, to talk about regular expression matching, optimisation, or other related topics, please use the [rejit-users][1] Google group or email me at <alexandre@uop.re> !

## Benchmarks

Below are a few sample performance results.<br />
Benchmarks were run on Fedora 17 with an Intel(R) Core(TM) i5-2400 CPU @ 3.10GHz with 4GB RAM.

#### Grepping recursively through the Linux kernel sources.

```
$ grep --version
grep (GNU grep) 2.14
$  grep -R regexp linux-3.9.2/ > /dev/null && time grep -R regexp linux-3.9.2/ > /dev/null
real  0m0.588s
user  0m0.362s
sys   0m0.221s
```

 ```jrep``` is a grep-like utility powered by rejit.

```
$ jrep -R regexp linux-3.9.2/ > /dev/null && time jrep -R regexp linux-3.9.2/ > /dev/null
real  0m0.345s
user  0m0.109s
sys   0m0.231s

```

#### DNA matching benchmark.

From the "[Computer Language Benchmarks Game][2]", this benchmark performs some DNA matching operations using regular expressions.

The table below shows performance for different input sizes, for the fastest registered single threaded implementation ([link][3]) and a single threaded rejit-powered implementation.
```
input size            v8 (3.9.9)         rejit
    50.000 (500KB)    0.032s             0.015s
   500.000 (  5MB)    0.232s             0.130s
 5.000.000 ( 50MB)    2.211s             1.423s
50.000.000 (500MB)    (out of memory)    14.805s
```

See performance for various engines and languages for [single-core][4] and [quad-core][5] implementations.

#### Complex regular expression matching
This is an example taken from rejit's benchmarks suite. It shows the performance to find all left-most longest matches of the regular expression ```([complex]|(regexp)){2,7}abcdefgh(at|the|[e-nd]as well)``` in randomly generated texts of various sizes. The performance reported is ```(<size of text> / <time to match>)```, which is easier to report and understand than close to zero time intervals.

It illustrates the 'fast forward' mechanism used by rejit (see TODO(link to article) for details).<br />
It shows

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
