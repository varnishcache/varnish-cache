..
	Copyright (c) 2012-2016 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license


Platform specific notes
------------------------

On some platforms it is necessary to adjust the operating system before running
Varnish on it. The systems and steps known to us are described in this section.

On Linux, use tmpfs for the workdir
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Varnish uses mapped files for shared memory, for which performance depends on
writes not blocking. On Linux, however, write throttling implemented by some
file systems (which is generally useful in other scenarios) can interact badly
with the way Varnish works and cause lockups or performance impacts. To avoid
such problems, it is recommended to use a ``tmpfs`` "virtual memory file system"
as the *workdir*.

To ensure ``tmpfs`` is used, check the following:

Determine the *workdir*. If you use a specific ``-n`` option to ``varnishd`` or
set the ``VARNISH_DEFAULT_N`` variable, it is that value. Otherwise run
``varnishd -x options``, which outputs the *workdir* default.

Run ``df *workdir*``. If it reports ``tmpfs`` as the file system in the first
column, no additional action is necessary.

Otherwise, consider creating a ``tmpfs`` mountpoint at *workdir*, or configure
*workdir* on an existing ``tmpfs``.

The ``tmpfs`` for *workdir* should be mounted with Transparent Hugepage
disabled. Consider mounting the working directory with the ``huge=never`` mount
option if that is not the default.

Note: Very valid reasons exist for *not* following this recommendation, if you
know what you are doing.

workdir can not be mounted ``noexec``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Varnish compiles VCL to a shared object and needs to load it at runtime. So the
*workdir* can not reside on a file system mounted with ``noexec``.

Lift locked memory limits
~~~~~~~~~~~~~~~~~~~~~~~~~

For the same reason as explained above, varnish tries to lock shared memory in
RAM. Therefore, the locked memory limit should ideally be set to unlimited or
sufficiently high to accommodate all mapped files. The specific minimum required
value is dynamic, depending among other factors on the number of VCLs loaded and
backends configured. As a rule of thumb, it should be a generous multiple of the
size of *workdir* when varnish is running.

See :ref:`ref-vsm` for details.

.. _platform-thp:

Transparent Hugepage on Linux
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

On certain Linux distributions Transparent Hugepage (THP) kernel support is
enabled by default. This is known to cause instabilities of Varnish.

By default, Varnish tries to disable the THP feature, but does not fail if it
can't. The ``linux`` :ref:`ref-varnishd-opt_j` offers to optionally enable,
disable or ignore THP.

Alternatively, THP can be disabled system-wide. If Varnish is the only
significant service running on this system, this can be done during runtime
with::

  echo never > /sys/kernel/mm/transparent_hugepage/enabled

The setting can be also be persisted in the bootloader configuration by adding
``transparent_hugepage=never`` to the kernel command line.

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
