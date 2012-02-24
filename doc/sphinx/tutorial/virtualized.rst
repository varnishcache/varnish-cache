
Running Varnish in a virtualized environment
--------------------------------------------

It is possible, but not recommended for high performance, to run
Varnish on virtualized hardware. Reduced disk- and network performance
will reduce the performance a bit so make sure your system has good IO
performance.

OpenVZ
~~~~~~

If you are running on 64bit OpenVZ (or Parallels VPS), you must reduce
the maximum stack size before starting Varnish. The default allocates
to much memory per thread, which will make varnish fail as soon as the
number of threads (==traffic) increases.

Reduce the maximum stack size by running::

    ulimit -s 256

in the startup script.

