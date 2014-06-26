.. _users_intro:

The Big Varnish Picture
=======================

In this section we will cover answers to the questions:
- What is in this package called "Varnish"?
- what are all the different bits and pieces named? 
- Will you need a hex-wrench for assembly?

The two main parts of Varnish are the two processes in the `varnishd`
program. The first process is called "the manager", and its job is to
talk to you, the administrator, and make the things you ask for
happen.

The second process is called "the worker" or just "the child" and
this is the process which does all the actual work with your HTTP
traffic.

When you start `varnishd`, you start the manager process, and once it is
done handling all the command line flags, it will start the child
process for you. Should the child process die, the manager will start
it again for you, automatically and right away.

The main reason for this division of labor is security: The manager
process will typically run with "root" permissions, in order to
open TCP socket port 80, but it starts the child process with minimal
permissions, as a defensive measure.

The manager process is interactive, it offers a CLI -- Command Line
Interface, which can be used manually, from scripts or programs. The
CLI offers almost full control of what Varnish actually does to your
HTTP traffic, and we have gone to great lengths to ensure that you
should not need to restart the Varnish processes, unless you need to
change something very fundamental.

The CLI can be safely accessed remotely, using a simple and flexible
PSK -- Pre Shared Key, access control scheme, so it is easy to
integrate Varnish into your operations and management infrastructure
or tie it to your CMS.

All this is covered in :ref:`users_running`.

Things like, how the child process should deal with the HTTP requests, what to
cache, which headers to remove etc, is all specified using a small
programming language called VCL -- Varnish Configuration Language.
The manager process will compile the VCL program and check it for
errors,

.. XXX:What does manager do after compile and error-check? Maybe a short description of further handling when no errors as well as when errors? benc

but it is the child process which runs the VCL program, for
each and every HTTP request which comes in.

Because the VCL is compiled to C code, and the C code is compiled
to machine instructions, even very complex VCL programs execute in
a few microseconds, without impacting performance at all.

And don't fret if you are not really a programmer, VCL is very
simple to do simple things with::

	sub vcl_recv {
		# Remove the cookie header to enable caching
		unset req.http.cookie;
	}

The CLI interface allows you to compile and load new VCL programs
at any time, and you can switch between the loaded VCL programs
instantly, without restarting the child process and without missing
a single HTTP request.

VCL code can be extended using external modules, called VMODs or
even by inline C-code if you are brave, so in terms of what Varnish
can do for your HTTP traffic, there really is no limit.

:ref:`users_vcl` describes VCL and what it can do in great detail.

Varnish uses a segment of shared memory to report and log its activities and
status. For each HTTP request, a number of very detailed records will
be appended to the log memory segment. Other processes
can subscribe to log-records, filter them, and format them, for
instance as Apache/NCSA style log records.

Another segment in shared memory is used for statistics counters,
this allows real-time, down to microsecond resolution monitoring
of cache hit-rate, resource usage and specific performance indicating
metrics.

Varnish comes with a number of tools which reports from shared
memory, `varnishlog`, `varnishstats`, `varnishncsa` etc, and with an API
library so you can write your own tools, should you need that.

:ref:`users_report` explains how all that work.

Presumably the reason for your interest in Varnish, is that you
want your website to work better. There are many aspects of
performance tuning a website, from relatively simple policy decisions
about what to cache, to designing a geographically diverse multilevel
CDNs using ESI and automatic failover.

.. XXX:CDNs or CDN? benc

:ref:`users_performance` will take you through the possibilities
and facilities Varnish offers.

Finally, Murphys Law must be referenced here: Things will go wrong, and
more likely than not, they will do so at zero-zero-dark O'clock. Most
likely during a hurricane, when your phone battery is flat and your
wife had prepared a intimate evening to celebrate your anniversary.

Yes, we've all been there, haven't we?

When things go wrong :ref:`users_trouble` will hopefully be of some help.

