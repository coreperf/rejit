Rejit is a prototype of a non-backtracking, just-in-time, SIMD-able regular
expression compiler developed on our free time. It is available under the GPLv3
licence. It currently only supports the x86_64 architecture.

## Documentation

Documentation and information is available [here][coreperf rejit]. Below are
some sample benchmarks results.

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
supports searching for multi-lines patterns and has initial multi-threading
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



  [2]: http://benchmarksgame.alioth.debian.org/
  [3]: http://benchmarksgame.alioth.debian.org/u64/program.php?test=regexdna&lang=v8&id=2
  [4]: http://benchmarksgame.alioth.debian.org/u64/benchmark.php?test=regexdna&lang=all&data=u64
  [5]: http://benchmarksgame.alioth.debian.org/u64q/benchmark.php?test=regexdna&lang=all&data=u64q
  [6]: tools/benchmarks/resources/sample_bench_complex.png
  [cpu_bench single threaded fastest]: http://benchmarksgame.alioth.debian.org/u64/program.php?test=regexdna&lang=v8&id=2
  [cpu_bench multi threaded fastest]: http://benchmarksgame.alioth.debian.org/u32q/program.php?test=regexdna&lang=gpp&id=2
  [coreperf rejit]: http://coreperf.com/projects/rejit/
