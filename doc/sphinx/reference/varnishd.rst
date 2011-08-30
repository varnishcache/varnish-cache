=========
 varnishd
=========

-----------------------
HTTP accelerator daemon
-----------------------

:Author: Dag-Erling Smørgrav
:Author: Stig Sandbeck Mathisen
:Author: Per Buer
:Date:   2010-05-31
:Version: 1.0
:Manual section: 1


SYNOPSIS
========

varnishd [-a address[:port]] [-b host[:port]] [-d] [-F] [-f config] 
	 [-g group] [-h type[,options]] [-i identity]
	 [-l shmlogsize] [-n name] [-P file] [-p param=value] 
	 [-s type[,options]] [-T address[:port]] [-t ttl]
	 [-u user] [-V] [-w min[,max[,timeout]]]

DESCRIPTION
===========

The varnishd daemon accepts HTTP requests from clients, passes them on to a backend server and caches the
returned documents to better satisfy future requests for the same document.

OPTIONS
=======

-a address[:port][,address[:port][...]
	    Listen for client requests on the specified address and port.  The address can be a host
            name (“localhost”), an IPv4 dotted-quad (“127.0.0.1”), or an IPv6 address enclosed in
            square brackets (“[::1]”).  If address is not specified, varnishd will listen on all
            available IPv4 and IPv6 interfaces.  If port is not specified, the default HTTP port as
            listed in /etc/services is used.  Multiple listening addresses and ports can be speci‐
            fied as a whitespace- or comma-separated list.

-b host[:port]
            Use the specified host as backend server.  If port is not specified, 
	    the default is 8080.

-C	    Print VCL code compiled to C language and exit. Specify the VCL file 
	    to compile with the -f option.

-d          Enables debugging mode: The parent process runs in the foreground with a CLI connection
            on stdin/stdout, and the child process must be started explicitly with a CLI command.
            Terminating the parent process will also terminate the child.

-F          Run in the foreground.

-f config   Use the specified VCL configuration file instead of the
            builtin default.  See vcl(7) for details on VCL
            syntax. When no configuration is supplied varnishd will
            not start the cache process.

-g group    Specifies the name of an unprivileged group to which the child process should switch
            before it starts accepting connections.  This is a shortcut for specifying the group
            run-time parameter.

-h type[,options]
            Specifies the hash algorithm.  See Hash Algorithms for a list of supported algorithms.

-i identity
            Specify the identity of the varnish server.  This can be accessed using server.identity
            from VCL

-l shmlogsize
            Specify size of shmlog file.  Scaling suffixes like 'k', 'm' can be used up to
            (e)tabytes.  Default is 80 Megabytes.  Specifying less than 8 Megabytes is unwise.

-n name     Specify a name for this instance.  Amonst other things, this name is used to construct
            the name of the directory in which varnishd keeps temporary files and persistent state.
            If the specified name begins with a forward slash, it is interpreted as the absolute
            path to the directory which should be used for this purpose.

-P file     Write the process's PID to the specified file.

-p param=value
            Set the parameter specified by param to the specified value.  See Run-Time 
	    Parameters for a list of parameters. This option can be used multiple 
	    times to specify multiple parameters.

-S file     Path to a file containing a secret used for authorizing access to the management port.

-s [name=]type[,options]
            Use the specified storage backend.  See Storage Types for a list of supported storage
            types.  This option can be used multiple times to specify multiple storage files. You
 	    can name the different backends. Varnish will then reference that backend with the 
	    given name in logs, statistics, etc.

-T address[:port]
            Offer a management interface on the specified address and port.  See Management
            Interface for a list of management commands.

-M address:port
            Connect to this port and offer the command line
            interface. Think of it as a reverse shell. When running with 
	    -M and there is no backend defined the child process (the cache)
            will not start initially.

-t ttl      
	    Specifies a hard minimum time to live for cached
            documents.  This is a shortcut for specifying the
            default_ttl run-time parameter.

-u user     Specifies the name of an unprivileged user to which the child
            process should switch before it starts accepting
            connections.  This is a shortcut for specifying the user
            run- time parameter.
	    
            If specifying both a user and a group, the user should be
            specified first.

-V          Display the version number and exit.

-w min[,max[,timeout]]

            Start at least min but no more than max worker threads
            with the specified idle timeout.  This is a shortcut for
            specifying the thread_pool_min, thread_pool_max and
            thread_pool_timeout run-time parameters.

            If only one number is specified, thread_pool_min and
            thread_pool_max are both set to this number, and
            thread_pool_timeout has no effect.





Hash Algorithms
---------------

The following hash algorithms are available:

simple_list
  A simple doubly-linked list.  Not recommended for production use.

classic[,buckets]
  A standard hash table.  This is the default.  The hash key is the
  CRC32 of the object's URL modulo the size of the hash table.  Each
  table entry points to a list of elements which share the same hash
  key. The buckets parameter specifies the number of entries in the
  hash table.  The default is 16383.

critbit
  A self-scaling tree structure. The default hash algorithm in 2.1. In
  comparison to a more traditional B tree the critbit tree is almost
  completely lockless.

Storage Types
-------------

The following storage types are available:

malloc[,size]
      Storage for each object is allocated with malloc(3).

      The size parameter specifies the maximum amount of memory varnishd will allocate.  The size is assumed to
      be in bytes, unless followed by one of the following suffixes:

      K, k    The size is expressed in kibibytes.

      M, m    The size is expressed in mebibytes.

      G, g    The size is expressed in gibibytes.

      T, t    The size is expressed in tebibytes.

      The default size is unlimited.

file[,path[,size[,granularity]]]
      Storage for each object is allocated from an arena backed by a file.  This is the default.

      The path parameter specifies either the path to the backing file or the path to a directory in which
      varnishd will create the backing file.  The default is /tmp.

      The size parameter specifies the size of the backing file.  The size is assumed to be in bytes, unless fol‐
      lowed by one of the following suffixes:

      K, k    The size is expressed in kibibytes.

      M, m    The size is expressed in mebibytes.

      G, g    The size is expressed in gibibytes.

      T, t    The size is expressed in tebibytes.

      %       The size is expressed as a percentage of the free space on the file system where it resides.

      The default size is 50%.

      If the backing file already exists, it will be truncated or expanded to the specified size.

      Note that if varnishd has to create or expand the file, it will not pre-allocate the added space, leading
      to fragmentation, which may adversely impact performance.  Pre-creating the storage file using dd(1) will
      reduce fragmentation to a minimum.

      The granularity parameter specifies the granularity of allocation.  All allocations are rounded up to this
      size.  The size is assumed to be in bytes, unless followed by one of the suffixes described for size except
      for %.

      The default size is the VM page size.  The size should be reduced if you have many small objects.

persistent,path,size {experimental}

      Persistent storage. Varnish will store objects in a file in a
      manner that will secure the survival of *most* of the objects in
      the event of a planned or unplanned shutdown of Varnish.

      The path parameter specifies the path to the backing file. If
      the file doesn't exist Varnish will create it.

      The size parameter specifies the size of the backing file.  The
      size is assumed to be in bytes, unless followed by one of the
      following suffixes:

      K, k    The size is expressed in kibibytes.

      M, m    The size is expressed in mebibytes.

      G, g    The size is expressed in gibibytes.

      T, t    The size is expressed in tebibytes.

      Varnish will split the file into logical *silos* and write to
      the silos in the manner of a circular buffer. Only one silo will
      be kept open at any given point in time. Full silos are
      *sealed*. When Varnish starts after a shutdown it will discard
      the content of any silo that isn't sealed.

Transient Storage
-----------------
      
      If you name any of your storage backend "Transient" it will be
      used for transient (short lived) objects. By default Varnish
      would use an unlimited malloc backend for this.

Management Interface
--------------------

If the -T option was specified, varnishd will offer a command-line management interface on the specified address
and port.  The recommended way of connecting to the command-line management interface is through varnishadm(1).

The commands available are documented in varnish(7).

Run-Time Parameters
-------------------

Runtime parameters are marked with shorthand flags to avoid repeating the same text over and over in the table
below.  The meaning of the flags are:

experimental
      We have no solid information about good/bad/optimal values for this parameter.  Feedback with experience
      and observations are most welcome.

delayed
      This parameter can be changed on the fly, but will not take effect immediately.

restart
      The worker process must be stopped and restarted, before this parameter takes effect.

reload
      The VCL programs must be reloaded for this parameter to take effect.

Here is a list of all parameters, current as of last time we remembered to update the manual page.  This text is
produced from the same text you will find in the CLI if you use the param.show command, so should there be a new
parameter which is not listed here, you can find the description using the CLI commands.

Be aware that on 32 bit systems, certain default values, such as sess_workspace (=16k) and thread_pool_stack
(=64k) are reduced relative to the values listed here, in order to conserve VM space.

.. include:: params.rst

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
with Verdens Gang AS, Varnish Software AS and Varnish Software.

This manual page was written by Dag-Erling Smørgrav with updates by
Stig Sandbeck Mathisen ⟨ssm@debian.org⟩


COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2007-2011 Varnish Software AS
