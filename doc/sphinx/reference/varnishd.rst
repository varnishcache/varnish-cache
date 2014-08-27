.. _ref-varnishd:

========
varnishd
========

-----------------------
HTTP accelerator daemon
-----------------------


SYNOPSIS
========

varnishd [-a address[:port]] [-b host[:port]] [-C] [-d] [-f config]
	 [-F] [-g group] [-h type[,options]] [-i identity]
	 [-l shl[,free[,fill]]] [-M address:port] [-n name] 
         [-P file] [-p param=value] [-r param[,param...]
	 [-s [name=]kind[,options]] [-S secret-file] [-T address[:port]] 
         [-t ttl] [-u user] [-V]

DESCRIPTION
===========

The varnishd daemon accepts HTTP requests from clients, passes them on
to a backend server and caches the returned documents to better
satisfy future requests for the same document.

.. _ref-varnishd-options:

OPTIONS
=======

-a address[:port][,address[:port][...]
            Listen for client requests on the specified address and
            port.  The address can be a host name (“localhost”), an
            IPv4 dotted-quad (“127.0.0.1”), or an IPv6 address
            enclosed in square brackets (“[::1]”).  If address is not
            specified, varnishd will listen on all available IPv4 and
            IPv6 interfaces.  If port is not specified, the default
            HTTP port as listed in /etc/services is used.  Multiple
            listening addresses and ports can be specified as a
            whitespace or comma -separated list.

-b host[:port]
            Use the specified host as backend server.  If port is not
	    specified, the default is 8080.

-C	    Print VCL code compiled to C language and exit. Specify the VCL file
	    to compile with the -f option.

-d          Enables debugging mode: The parent process runs in the foreground
            with a CLI connection on stdin/stdout, and the child
            process must be started explicitly with a CLI command.
            Terminating the parent process will also terminate the
            child.

-f config   Use the specified VCL configuration file instead of the
            builtin default.  See vcl(7) for details on VCL
            syntax. When no configuration is supplied varnishd will
            not start the cache process.

-F          Run in the foreground.

-g group    Specifies the name of an unprivileged group to which the
            child process should switch before it starts accepting
            connections.  This is a shortcut for specifying the group
            run-time parameter.

-h type[,options]
            Specifies the hash algorithm.  See Hash Algorithms for a list of supported algorithms.

-i identity
            Specify the identity of the Varnish server.  This can be accessed using server.identity
            from VCL

-l shl[,free[,fill]]
            Specifies size of shmlog file. shl is the store for the
            shared memory log records [80M], free is the store for other
            allocations [1M] and fill determines how the log is [+].
            Scaling suffixes like 'k', 'M' can be used up to
            (E)xabytes.  Default is 80 Megabytes.

-M address:port
            Connect to this port and offer the command line interface.
            Think of it as a reverse shell. When running with -M and there is
            no backend defined the child process (the cache) will not start
            initially.

-n name     Specify the name for this instance.  Amonst other things, this
            name is used to construct the name of the directory in
            which varnishd keeps temporary files and persistent state.
            If the specified name begins with a forward slash, it is
            interpreted as the absolute path to the directory which
            should be used for this purpose.

-P file     Write the process's PID to the specified file.

-p param=value
            Set the parameter specified by param to the specified value.  See
            Run-Time Parameters for a list of parameters. This option can be
            used multiple times to specify multiple parameters.

-r param[,param...]
            Make the listed parameters read only. This gives the
            system administrator a way to limit what the Varnish CLI can do.
            Consider making parameters such as *user*, *group*, *cc_command*,
            *vcc_allow_inline_c* read only as these can potentially be used
            to escalate privileges from the CLI.
            Protecting *listen_address* may also be a good idea.

-s [name=]type[,options]
            Use the specified storage backend. The storage backends can be one of the following:
               * malloc[,size]
               * file[,path[,size[,granularity]]]
               * persistent,path,size

            See Storage Types in the Users Guide for more information
            on the various storage backends.  This option can be used
            multiple times to specify multiple storage files. Names
            are referenced in logs, vcl, statistics, etc.

-S file     Path to a file containing a secret used for authorizing access to the management port.

-T address[:port]
            Offer a management interface on the specified address and port.  See Management
            Interface for a list of management commands.

-t ttl      Specifies a hard minimum time to live for cached documents. This
            is a shortcut for specifying the default_ttl run-time parameter.

-u user     Specifies the name of an unprivileged user to which the child
            process should switch before it starts accepting
            connections. This is a shortcut for specifying the user
            runtime parameter.

            If specifying both a user and a group, the user should be
            specified first.

-V          Display the version number and exit.


Hash Algorithms
---------------

The following hash algorithms are available:

critbit 
   A self-scaling tree structure. The default hash algorithm in
    Varnish Cache 2.1 and onwards. In comparison to a more traditional
    B tree the critbit tree is almost completely lockless. Do not 
    change this unless you are certain what you're doing.

simple_list
    A simple doubly-linked list.  Not recommended for production use.

classic[,buckets]
    A standard hash table. The hash key is the CRC32 of the object's
    URL modulo the size of the hash table.  Each table entry points to
    a list of elements which share the same hash key. The buckets
    parameter specifies the number of entries in the hash table.  The
    default is 16383.


Storage Types
-------------

The following storage types are available:

malloc
~~~~~~

syntax: malloc[,size]

malloc is a memory based backend.

file
~~~~

syntax: file[,path[,size[,granularity]]]

The file backend stores data in a file on disk. The file will be accessed using mmap.

persistent (experimental)
~~~~~~~~~~~~~~~~~~~~~~~~~

syntax: persistent,path,size

Persistent storage. Varnish will store objects in a file in a manner
that will secure the survival of *most* of the objects in the event of
a planned or unplanned shutdown of Varnish. The persistent storage
backend has multiple issues with it and will likely be removed from a
future version of Varnish.


Management Interface
--------------------

If the -T option was specified, varnishd will offer a command-line
management interface on the specified address and port.  The
recommended way of connecting to the command-line management interface
is through varnishadm(1).

The commands available are documented in varnish(7).

Run-Time Parameters
-------------------

Runtime parameters are marked with shorthand flags to avoid repeating
the same text over and over in the table below.  The meaning of the
flags are:

experimental
      We have no solid information about good/bad/optimal values for
      this parameter.  Feedback with experience and observations are
      most welcome.

delayed
      This parameter can be changed on the fly, but will not take
      effect immediately.

restart
      The worker process must be stopped and restarted, before this
      parameter takes effect.

reload
      The VCL programs must be reloaded for this parameter to take effect.

experimental
      We're not really sure about this parameter, tell us what you find.

wizard
      Do not touch unless you *really* know what you're doing.

only_root
      Only works if varnishd is running as root.

Here is a list of all parameters, current as of last time we
remembered to update the manual page.  This text is produced from the
same text you will find in the CLI if you use the param.show command,
so should there be a new parameter which is not listed here, you can
find the description using the CLI commands.

Be aware that on 32 bit systems, certain default values, such as
workspace_client (=16k), thread_pool_workspace (=16k), http_resp_size
(=8k), http_req_size (=12k), gzip_stack_buffer (=4k) and
thread_pool_stack (=64k) are reduced relative to the values listed
here, in order to conserve VM space.

.. include:: ../include/params.rst

EXIT CODES
==========

Varnish and bundled tools will, in most cases, exit with one of the
following codes

* `0` OK
* `1` Some error which could be system-dependend and/or transient
* `2` Serious configuration / parameter error - retrying with the same
  configuration / parameters is most likely useless

The `varnishd` master process may also OR its exit code

* with `0x20` when the `varnishd` child process died,
* with `0x40` when the `varnishd` child process was terminated by a
  signal and
* with `0x80` when a core was dumped.

SEE ALSO
========

* varnish-cli(7)
* varnishlog(1)
* varnishhist(1)
* varnishncsa(1)
* varnishstat(1)
* varnishtop(1)
* vcl(7)

HISTORY
=======

The varnishd daemon was developed by Poul-Henning Kamp in cooperation
with Verdens Gang AS and Varnish Software.

This manual page was written by Dag-Erling Smørgrav with updates by
Stig Sandbeck Mathisen <ssm@debian.org>.


COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2007-2014 Varnish Software AS
