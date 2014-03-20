
Platform specific notes
------------------------

On some platforms it is necessary to adjust the operating system before running
Varnish on it. The systems and steps known to us are described in this section.


Transparent hugepages on Redhat Enterprise Linux 6
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

On RHEL6 Transparent Hugepage kernel support is enabled by default.
This is known to cause sporadic crashes of Varnish.

It is recommended to disable transparent hugepages on affected systems. This
can be done with 
``echo "never" > /sys/kernel/mm/redhat_transparent_hugepage/enabled`` (runtime) and changes to
`/etc/sysctl.conf` (persisted.)

On Debian/Ubuntu systems running 3.2 kernels the default value is "madvise" and
does not need to be changed.


OpenVZ
~~~~~~

It is possible, but not recommended for high performance, to run
Varnish on virtualised hardware. Reduced disk and network -performance
will reduce the performance a bit so make sure your system has good IO
performance.

If you are running on 64bit OpenVZ (or Parallels VPS), you must reduce
the maximum stack size before starting Varnish.

The default allocates too much memory per thread, which will make Varnish fail
as soon as the number of threads (traffic) increases.

Reduce the maximum stack size by adding ``ulimit -s 256`` before starting
Varnish in the init script.

TCP keep-alive configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

On some Solaris, FreeBSD and OS X systems, Varnish is not able to set the TCP
keep-alive values per socket, and therefore the *tcp_keepalive_* Varnish runtime
parameters are not available. On these platforms it can be beneficial to tune
the system wide values for these in order to more reliably detect remote close
for sessions spending long time on waitinglists. This will help free up
resources faster.

Systems that does not support TCP keep-alive values per socket include:

- Solaris releases prior to version 11
- FreeBSD releases prior to version 9.1
- OS X releases prior to Mountain Lion

On platforms with the necessary socket options the defaults are set
to:

- `tcp_keepalive_time` = 600 seconds
- `tcp_keepalive_probes` = 5
- `tcp_keepalive_intvl` = 5 seconds

Note that Varnish will only apply these run-time parameters so long as
they are less than the system default value.

.. XXX:Maybe a sample-command of using/setting/changing these values? benc
