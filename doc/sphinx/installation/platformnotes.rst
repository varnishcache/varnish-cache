..
	Copyright (c) 2012-2016 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license


Platform specific notes
------------------------

On some platforms it is necessary to adjust the operating system before running
Varnish on it. The systems and steps known to us are described in this section.


Transparent Hugepage on Linux
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

On certain Linux distributions Transparent Hugepage kernel support is enabled
by default. This is known to cause instabilities of Varnish.

It is recommended to disable Transparent Hugepage on affected systems.
If Varnish is the only significant service running on this system, this can be
done during runtime with::

  echo never > /sys/kernel/mm/transparent_hugepage/enabled

Alternatively, this can be persisted in your bootloader configuration by adding
``transparent_hugepage=never`` to the kernel command line.

On other systems the default value is ``madvise`` and does not need to be
changed. Either way, the Linux :ref:`ref-varnishd-opt_j` will try to disable
Transparent Hugepage in the ``varnishd`` process.

The general recommendation is to mount the working directory in a ``tmpfs``
partition, usually ``/var/lib/varnish``. By default tmpfs should be mounted with
Transparent Hugepage disabled. Consider mounting the working directory with the
``huge=never`` mount option if that is not the case with your OS vendor.

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
