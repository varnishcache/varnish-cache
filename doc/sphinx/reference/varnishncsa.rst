.. _ref-varnishncsa:

===========
varnishncsa
===========

---------------------------------------------------------
Display Varnish logs in Apache / NCSA combined log format
---------------------------------------------------------

SYNOPSIS
========

.. include:: ../include/varnishncsa_synopsis.rst
varnishncsa |synopsis|

DESCRIPTION
===========

The varnishncsa utility reads varnishd(1) shared memory logs and
presents them in the Apache / NCSA "combined" log format.

Each log line produced is based on a single Request type transaction
gathered from the shared memory log. The Request transaction is then
scanned for the relevant parts in order to output one log line. To
filter the log lines produced, use the query language to select the
applicable transactions. Non-request transactions are ignored.

The following options are available:

.. include:: ../include/varnishncsa_options.rst

FORMAT
======

Specify the log format used. If no format is specified the default log
format is used.

The default log format is::

  %h %l %u %t "%r" %s %b "%{Referer}i" "%{User-agent}i"

Escape sequences \\n and \\t are supported.

Supported formatters are:

%b
  Size of response in bytes, excluding HTTP headers.  In CLF format,
  i.e. a '-' rather than a 0 when no bytes are sent.

%D
  Time taken to serve the request, in microseconds.

%H
  The request protocol. Defaults to HTTP/1.0 if not known.

%h
  Remote host. Defaults to '-' if not known.

%I
  Total bytes received from client.

%{X}i
  The contents of request header X.

%l
   Remote logname (always '-')

%m
   Request method. Defaults to '-' if not known.

%{X}o
  The contents of response header X.

%O
  Total bytes sent to client.

%q
  The query string, if no query string exists, an empty string.

%r
  The first line of the request. Synthesized from other fields, so it
  may not be the request verbatim.

%s
  Status sent to the client

%t
  Time when the request was received, in HTTP date/time format.

%{X}t
  Time when the request was received, in the format specified
  by X. The time specification format is the same as for strftime(3).

%T
  Time taken to serve the request, in seconds.

%U
  The request URL without any query string. Defaults to '-' if not
  known.

%u
  Remote user from auth

%{X}x
  Extended variables.  Supported variables are:

  Varnish:time_firstbyte
    Time from when the request processing starts until the first byte
    is sent to the client.

  Varnish:hitmiss
    Whether the request was a cache hit or miss. Pipe and pass are
    considered misses.

  Varnish:handling
    How the request was handled, whether it was a cache hit, miss,
    pass, pipe or error.

  VCL_Log:key
    Output value set by std.log("key:value") in VCL.

SIGNALS
=======

SIGHUP
  Rotate the log file (see -w option)

SIGUSR1
  Flush any outstanding transactions

SEE ALSO
========

varnishd(1)
varnishlog(1)
varnishstat(1)

HISTORY
=======

The varnishncsa utility was developed by Poul-Henning Kamp in
cooperation with Verdens Gang AS and Varnish Software AS. This manual page was
initially written by Dag-Erling Smørgrav <des@des.no>, and later updated
by Martin Blix Grydeland.


COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2006 Verdens Gang AS
* Copyright (c) 2006-2014 Varnish Software AS
