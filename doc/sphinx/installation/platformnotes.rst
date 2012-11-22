
Platform specific notes
------------------------

Transparent hugepages on Redhat Enterprise Linux 6
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

On RHEL6 Transparent Hugepage kernel support is enabled by default.
This is known to cause sporadic crashes of Varnish.

It is recommended to disable transparent hugepages on affected systems::

    $ echo "never" > /sys/kernel/mm/redhat_transparent_hugepage/enabled

On Debian/Ubuntu systems running 3.2 kernels the default value is "madvise" and does not need to changed.


OpenVZ
~~~~~~

It is possible, but not recommended for high performance, to run
Varnish on virtualised hardware. Reduced disk and network -performance
will reduce the performance a bit so make sure your system has good IO
performance.

If you are running on 64bit OpenVZ (or Parallels VPS), you must reduce
the maximum stack size before starting Varnish.

The default allocates to much memory per thread, which will make varnish fail
as soon as the number of threads (traffic) increases.

Reduce the maximum stack size by running::

    ulimit -s 256

in the Varnish startup script.

