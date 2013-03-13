rejit regular expression (JIT) compiler by Alexandre Rames <alexandre@uop.re>.<br />
This a proof of concept implementation. It is not ready for production.

## Quickstart - Benchmarks and documentation

Run the benchmarks and open the results along with some documentation.
```
$ ./tools/benchmarks/run.py
$ <browser> ./html/rejit.html
```

## Bulding and usage

All scripts and utilities should include an help message available via

```$ <script> --help```

You can build the rejit library with
```$ scons```

Include include/rejit.h in your program. Some documentation is available as comments in the header file.

Once the library is built you can link it with your executable.
```
  $ g++ -o myprg myprg.cc -I<rejit>/include -L<rejit>/build/release -lrejit
```

A basic example sample/basic.cc is included. It is automatically compiled when
running scons if you want to play with it.
You can manually compile it with:

```
  $ scons
  $ g++ -o basic sample/basic.cc -Iinclude -L/build/release -lrejit
```

## Testing

Run the rejit test suite with
```$ ./tools/tests/run.py```


## Benchmarking

Run the set of benchmarks with ```$ ./tools/benchmarks/run.py```.
The results along with some description of rejit will be available as an html
file html/rejit.html.


If you want to look at regexp performances, you can use rejit's benchmark
utilities.
You can build them with ```$ scons benchmark```.

This will produce the executables

```
  tools/benchmarks/resources/engines/rejit/engine
  tools/benchmarks/resources/engines/re2/engine
```
which you can use for example with
```
  $ tools/benchmarks/resources/engines/rejit/engine 'regexp'
```
.
Speed is reported in bytes per second.

**WARNING**:
These utilities are designed for the automated benchmarks, and not
currently very adapted to manual benchmarking.  Please read
```$ tools/benchmarks/resources/engines/rejit/engine --help``` carefully!
