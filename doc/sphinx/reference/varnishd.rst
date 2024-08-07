..
	Copyright (c) 2010-2020 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. role:: ref(emphasis)

.. _varnishd(1):

========
varnishd
========

-----------------------
HTTP accelerator daemon
-----------------------

:Manual section: 1

SYNOPSIS
========

varnishd
    [-a [name=][listen_address[,PROTO]]
    [-b [host[:port]|path]]
    [-C]
    [-d]
    [-F]
    [-f config]
    [-h type[,options]]
    [-I clifile]
    [-i identity]
    [-j jail[,jailoptions]]
    [-l vsl]
    [-M address:port]
    [-n workdir]
    [-P file]
    [-p param=value]
    [-r param[,param...]]
    [-S secret-file]
    [-s [name=]kind[,options]]
    [-T address[:port]]
    [-t TTL]
    [-V]
    [-W waiter]

varnishd [-x parameter|vsl|cli|builtin|optstring]

varnishd [-?]

DESCRIPTION
===========

The `varnishd` daemon accepts HTTP requests from clients, passes them on
to a backend server and caches the returned documents to better
satisfy future requests for the same document.

.. _ref-varnishd-options:

OPTIONS
=======

Basic options
-------------

-a <[name=][listen_address[,PROTO]]>

  Accept for client requests on the specified listen_address (see below).

  Name is referenced in logs. If name is not specified, "a0", "a1",
  etc. is used.

  PROTO can be "HTTP" (the default) or "PROXY".  Both version 1
  and 2 of the proxy protocol can be used.

  Multiple -a arguments are allowed.

  If no -a argument is given, the default `-a :80` will listen to
  all IPv4 and IPv6 interfaces.

-a <[name=][ip_address][:port][,PROTO]>

  The ip_address can be a host name ("localhost"), an IPv4 dotted-quad
  ("127.0.0.1") or an IPv6 address enclosed in square brackets
  ("[::1]")

  If port is not specified, port 80 (http) is used.

  At least one of ip_address or port is required.

-a <[name=][path][,PROTO][,user=name][,group=name][,mode=octal]>

  (VCL4.1 and higher)

  Accept connections on a Unix domain socket.  Path must be absolute
  ("/path/to/listen.sock") or "@" followed by the name of an abstract
  socket ("@myvarnishd").

  The user, group and mode sub-arguments may be used to specify the
  permissions of the socket file -- use names for user and group, and
  a 3-digit octal value for mode. These sub-arguments do not apply to
  abstract sockets.

-b <[host[:port]|path]>

  Use the specified host as backend server. If port is not specified,
  the default is 8080.

  If the value of ``-b`` begins with ``/``, it is interpreted as the
  absolute path of a Unix domain socket to which Varnish connects. In
  that case, the value of ``-b`` must satisfy the conditions required
  for the ``.path`` field of a backend declaration, see :ref:`vcl(7)`.
  Backends with Unix socket addresses may only be used with VCL
  versions >= 4.1.

  -b can be used only once, and not together with f.

-f config

  Use the specified VCL configuration file instead of the builtin
  default.  See :ref:`vcl(7)` for details on VCL syntax.

  If a single -f option is used, then the VCL instance loaded from the
  file is named "boot" and immediately becomes active. If more than
  one -f option is used, the VCL instances are named "boot0", "boot1"
  and so forth, in the order corresponding to the -f arguments, and
  the last one is named "boot", which becomes active.

  Either -b or one or more -f options must be specified, but not both,
  and they cannot both be left out, unless -d is used to start
  `varnishd` in debugging mode. If the empty string is specified as
  the sole -f option, then `varnishd` starts without starting the
  worker process, and the management process will accept CLI commands.
  You can also combine an empty -f option with an initialization
  script (-I option) and the child process will be started if there
  is an active VCL at the end of the initialization.

  When used with a relative file name, config is searched in the
  ``vcl_path``. It is possible to set this path prior to using ``-f``
  options with a ``-p`` option. During startup, `varnishd` doesn't
  complain about unsafe VCL paths: unlike the `varnish-cli(7)` that
  could later be accessed remotely, starting `varnishd` requires
  local privileges.

.. include:: ../include/options.rst

Documentation options
---------------------

For these options, `varnishd` prints information to standard output
and exits. When a -x option is used, it must be the only option (it
outputs documentation in reStructuredText, aka RST).

-?

  Print the usage message.

-x parameter

  Print documentation of the runtime parameters (-p options), see
  `List of Parameters`_.

-x vsl

  Print documentation of the tags used in the Varnish shared memory
  log, see :ref:`vsl(7)`.

-x cli

  Print documentation of the command line interface, see
  :ref:`varnish-cli(7)`.

-x builtin

  Print the contents of the default VCL program ``builtin.vcl``.

-x optstring

  Print the optstring parameter to ``getopt(3)`` to help writing
  wrapper scripts.

Operations options
------------------

-F

  Do not fork, run in the foreground. Only one of -F or -d can be
  specified, and -F cannot be used together with -C.

-T <address[:port]>

  Offer a management interface on the specified address and port. See
  :ref:`varnish-cli(7)` for documentation of the management commands.
  To disable the management interface use ``none``.

-M <address:port>

  Connect to this port and offer the command line interface.  Think of
  it as a reverse shell. When running with -M and there is no backend
  defined the child process (the cache) will not start initially.

-P file

  Write the PID of the process to the specified file.

-i identity

  Specify the identity of the Varnish server. This can be accessed
  using ``server.identity`` from VCL.

  The server identity is used for the ``received-by`` field of ``Via``
  headers generated by Varnish. For this reason, it must be a valid
  token as defined by the HTTP grammar.

  If not specified the output of ``gethostname(3)`` is used, in which
  case the syntax is assumed to be correct.

-I clifile

  Execute the management commands in the file given as ``clifile``
  before the worker process starts, see `CLI Command File`_.

Tuning options
--------------

-t TTL

  Specifies the default time to live (TTL) for cached objects. This is
  a shortcut for specifying the *default_ttl* run-time parameter.

-p <param=value>

  Set the parameter specified by param to the specified value, see
  `List of Parameters`_ for details. This option can be used multiple
  times to specify multiple parameters.

-s <[name=]type[,options]>

  Use the specified storage backend. See `Storage Backend`_ section.

  This option can be used multiple times to specify multiple storage
  files. Name is referenced in logs, VCL, statistics, etc. If name
  is not specified, "s0", "s1" and so forth is used.

-l <vsl>

  Specifies size of the space for the VSL records, shorthand for
  ``-p vsl_space=<vsl>``. Scaling suffixes like 'K' and 'M' can be
  used up to (G)igabytes. See `vsl_space`_ for more information.

Security options
----------------

-r <param[,param...]>

  Make the listed parameters read only. This gives the system
  administrator a way to limit what the Varnish CLI can do.  Consider
  making parameters such as *cc_command*, *vcc_allow_inline_c* and
  *vmod_path* read only as these can potentially be used to escalate
  privileges from the CLI.

-S secret-file

  Path to a file containing a secret used for authorizing access to
  the management port. To disable authentication use ``none``.

  If this argument is not provided, a secret drawn from the system
  PRNG will be written to a file called ``_.secret`` in the working
  directory (see `opt_n`_) with default ownership and permissions of
  the user having started varnish.

  Thus, users wishing to delegate control over varnish will probably
  want to create a custom secret file with appropriate permissions
  (ie. readable by a unix group to delegate control to).

-j <jail[,jailoptions]>

  Specify the jailing mechanism to use. See `Jail`_ section.

Advanced, development and debugging options
-------------------------------------------

-d

  Enables debugging mode: The parent process runs in the foreground
  with a CLI connection on stdin/stdout, and the child process must be
  started explicitly with a CLI command. Terminating the parent
  process will also terminate the child.

  Only one of -d or -F can be specified, and -d cannot be used together
  with -C.

-C

  Print VCL code compiled to C language and exit. Specify the VCL file
  to compile with the -f option. Either -f or -b must be used with -C,
  and -C cannot be used with -F or -d.

-V

  Display the version number and exit. This must be the only option.

-h <type[,options]>

  Specifies the hash algorithm. See `Hash Algorithm`_ section for a list
  of supported algorithms.

-W waiter

  Specifies the waiter type to use.

.. _opt_h:

Hash Algorithm
--------------

The following hash algorithms are available:

-h critbit

  self-scaling tree structure. The default hash algorithm in Varnish
  Cache 2.1 and onwards. In comparison to a more traditional B tree
  the critbit tree is almost completely lockless. Do not change this
  unless you are certain what you're doing.

-h simple_list

  A simple doubly-linked list.  Not recommended for production use.

-h <classic[,buckets]>

  A standard hash table. The hash key is the CRC32 of the object's URL
  modulo the size of the hash table.  Each table entry points to a
  list of elements which share the same hash key. The buckets
  parameter specifies the number of entries in the hash table.  The
  default is 16383.


.. _ref-varnishd-opt_s:

Storage Backend
---------------

The argument format to define storage backends is:

-s <[name]=kind[,options]>

  If *name* is omitted, Varnish will name storages ``s``\ *N*,
  starting with ``s0`` and incrementing *N* for every new storage.

  For *kind* and *options* see details below.

Storages can be used in vcl as ``storage.``\ *name*, so, for
example if ``myStorage`` was defined by ``-s myStorage=malloc,5G``, it
could be used in VCL like so::

  set beresp.storage = storage.myStorage;

A special *name* is ``Transient`` which is the default storage for
uncacheable objects as resulting from a pass, hit-for-miss or
hit-for-pass.

If no ``-s`` options are given, the default is::

	-s default,100m

If no ``Transient`` storage is defined, the default is an unbound
``default`` storage as if defined as::

	-s Transient=default


The following storage types and options are available:

-s <default[,size]>

  The default storage type resolves to ``umem`` where available and
  ``malloc`` otherwise.

-s <malloc[,size]>

  malloc is a memory based backend.

-s <umem[,size]>

  umem is a storage backend which is more efficient than malloc on
  platforms where it is available.

  See the section on umem in chapter `Storage backends` of `The
  Varnish Users Guide` for details.

-s <file,path[,size[,granularity[,advice]]]>

  The file backend stores data in a file on disk. The file will be
  accessed using mmap. Note that this storage provide no cache persistence.

  The path is mandatory. If path points to a directory, a temporary
  file will be created in that directory and immediately unlinked. If
  path points to a non-existing file, the file will be created.

  If size is omitted, and path points to an existing file with a size
  greater than zero, the size of that file will be used. If not, an
  error is reported.

  Granularity sets the allocation block size. Defaults to the system
  page size or the filesystem block size, whichever is larger.

  Advice tells the kernel how `varnishd` expects to use this mapped
  region so that the kernel can choose the appropriate read-ahead
  and caching techniques. Possible values are ``normal``, ``random``
  and ``sequential``, corresponding to MADV_NORMAL, MADV_RANDOM and
  MADV_SEQUENTIAL madvise() advice argument, respectively. Defaults to
  ``random``.

-s <persistent,path,size>

  Persistent storage. Varnish will store objects in a file in a manner
  that will secure the survival of *most* of the objects in the event
  of a planned or unplanned shutdown of Varnish. The persistent
  storage backend has multiple issues with it and will likely be
  removed from a future version of Varnish.

.. _ref-varnishd-opt_j:

Jail
----

Varnish jails are a generalization over various platform specific
methods to reduce the privileges of varnish processes. They may have
specific options. Available jails are:

-j <solaris[,worker=`privspec`]>

  Reduce `privileges(5)` for `varnishd` and sub-processes to the
  minimally required set. Only available on platforms which have the
  `setppriv(2)` call.

  The optional `worker` argument can be used to pass a
  privilege-specification (see `ppriv(1)`) by which to extend the
  effective set of the varnish worker process. While extended
  privileges may be required by custom vmods, *not* using the `worker`
  option is always more secure.

  Example to grant basic privileges to the worker process::

    -j solaris,worker=basic

-j <unix[,user=`user`][,ccgroup=`group`][,workuser=`user`]>

  Default on all other platforms when `varnishd` is started with an
  effective uid of 0 ("as root").

  With the ``unix`` jail mechanism activated, varnish will switch to
  an alternative user for subprocesses and change the effective uid of
  the master process whenever possible.

  The optional `user` argument specifies which alternative user to
  use. It defaults to ``varnish``.

  The optional `ccgroup` argument specifies a group to add to varnish
  subprocesses requiring access to a c-compiler. There is no default.

  The optional `workuser` argument specifies an alternative user to use
  for the worker process. It defaults to ``vcache``.

  The users given for the `user` and `workuser` arguments need to have
  the same primary ("login") group.

  To set up a system for the default users with a group name
  ``varnish``, shell commands similar to these may be used::

    groupadd varnish
    useradd -g varnish -d /nonexistent -s /bin/false \
      -c "Varnish-Cache Daemon User" varnish
    useradd -g varnish -d /nonexistent -s /bin/false \
      -c "Varnish-Cache Worker User" vcache

-j none

  last resort jail choice: With jail mechanism ``none``, varnish will
  run all processes with the privileges it was started with.


.. _ref-varnishd-opt_T:

Management Interface
--------------------

If the -T option was specified, `varnishd` will offer a command-line
management interface on the specified address and port.  The
recommended way of connecting to the command-line management interface
is through :ref:`varnishadm(1)`.

The commands available are documented in :ref:`varnish-cli(7)`.

CLI Command File
----------------

The -I option makes it possible to run arbitrary management commands
when `varnishd` is launched, before the worker process is started. In
particular, this is the way to load configurations, apply labels to
them, and make a VCL instance active that uses those labels on
startup::

  vcl.load panic /etc/varnish_panic.vcl
  vcl.load siteA0 /etc/varnish_siteA.vcl
  vcl.load siteB0 /etc/varnish_siteB.vcl
  vcl.load siteC0 /etc/varnish_siteC.vcl
  vcl.label siteA siteA0
  vcl.label siteB siteB0
  vcl.label siteC siteC0
  vcl.load main /etc/varnish_main.vcl
  vcl.use main

Every line in the file, including the last line, must be terminated by
a newline or carriage return or is otherwise considered truncated, which
is a fatal error.

If a command in the file is prefixed with '-', failure will not abort
the startup.

Note that it is necessary to include an explicit `vcl.use` command to
select which VCL should be the active VCL when relying on CLI Command File
to load the configurations at startup.

.. _ref-varnishd-params:

RUN TIME PARAMETERS
===================

Runtime parameters can either be set during startup with the ``-p`` command
line option for ``varnishd(1)`` or through the CLI using the ``param.set`` or
``param.reset`` commands. They can be locked during startup with the ``-r``
command line option.

Run Time Parameter Units
------------------------

There are different types of parameters that may accept a list of specific
values, or optionally take a unit suffix.

bool
~~~~

A boolean parameter accepts the values ``on`` and ``off``.

It will also recognize the following values:

- ``yes`` and ``no``
- ``true`` and ``false``
- ``enable`` and ``disable``

bytes
~~~~~

A bytes parameter requires one of the following units suffixes:

- ``b`` (bytes)
- ``k`` (kibibytes, 1024 bytes)
- ``m`` (mebibytes, 1024 kibibytes)
- ``g`` (gibibytes, 1024 mebibytes)
- ``t`` (tebibytes, 1024 gibibytes)
- ``p`` (pebibytes, 1024 tebibytes)

Multiplicator units may be appended with an extra ``b``. For example ``32k``
is equivalent to ``32kb``. Bytes units are case-insensitive.

seconds
~~~~~~~

A duration parameter may accept the following units suffixes:

- ``ms`` (milliseconds)
- ``s`` (seconds)
- ``m`` (minutes)
- ``h`` (hours)
- ``d`` (days)
- ``w`` (weeks)
- ``y`` (years)

If the parameter is a timeout or a deadline, a value of "never" (when allowed)
disables the effect of the parameter.

Run Time Parameter Flags
------------------------

Runtime parameters are marked with shorthand flags to avoid repeating
the same text over and over in the table below. The meaning of the
flags are:

* `experimental`

  We have no solid information about good/bad/optimal values for this
  parameter. Feedback with experience and observations are most
  welcome.

* `delayed`

  This parameter can be changed on the fly, but will not take effect
  immediately.

* `restart`

  The worker process must be stopped and restarted, before this
  parameter takes effect.

* `reload`

  The VCL programs must be reloaded for this parameter to take effect.

* `wizard`

  Do not touch unless you *really* know what you're doing.

* `only_root`

  Only works if `varnishd` is running as root.

Default Value Exceptions on 32 bit Systems
------------------------------------------

Be aware that on 32 bit systems, certain default or maximum values are
reduced relative to the values listed below, in order to conserve VM
space:

* workspace_client: 24k
* workspace_backend: 20k
* http_resp_size: 8k
* http_req_size: 12k
* gzip_buffer: 4k
* vsl_buffer: 4k
* vsl_space: 1G (maximum)
* thread_pool_stack: 64k

.. _List of Parameters:

List of Parameters
------------------

This text is produced from the same text you will find in the CLI if
you use the param.show command:

.. include:: ../include/params.rst

EXIT CODES
==========

Varnish and bundled tools will, in most cases, exit with one of the
following codes

* `0` OK
* `1` Some error which could be system-dependent and/or transient
* `2` Serious configuration / parameter error - retrying with the same
  configuration / parameters is most likely useless

The `varnishd` master process may also OR its exit code

* with `0x20` when the `varnishd` child process died,
* with `0x40` when the `varnishd` child process was terminated by a
  signal and
* with `0x80` when a core was dumped.

SEE ALSO
========

* :ref:`varnishlog(1)`
* :ref:`varnishhist(1)`
* :ref:`varnishncsa(1)`
* :ref:`varnishstat(1)`
* :ref:`varnishtop(1)`
* :ref:`varnish-cli(7)`
* :ref:`vcl(7)`

HISTORY
=======

The `varnishd` daemon was developed by Poul-Henning Kamp in cooperation
with Verdens Gang AS and Varnish Software.

This manual page was written by Dag-Erling Smørgrav with updates by
Stig Sandbeck Mathisen <ssm@debian.org>, Nils Goroll and others.


COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2007-2015 Varnish Software AS
