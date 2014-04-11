.. _ref-varnishtest:

===========
varnishtest
===========

------------------------
Test program for Varnish
------------------------

SYNOPSIS
========

     varnishtest [-iklLqv] [-n iter] [-D name=val] [-j jobs] [-t duration] file [file ...]

DESCRIPTION
===========

The varnishtest program is a script driven program used to test the
Varnish Cache.

The varnishtest program, when started and given one or more script
files, can create a number of threads representing backends, some
threads representing clients, and a varnishd process. This is then used to
simulate a transaction to provoke a specific behavior.

The following options are available:

-D name=val      Define macro for use in scripts

-i               Find varnishd in build tree

-j jobs          Run this many tests in parallel

-k               Continue on test failure

-l               Leave temporary vtc.* if test fails

-L               Always leave temporary vtc.*

-n iterations    Run tests this many times

-q               Quiet mode: report only failures

-t duration      Time tests out after this long

-v               Verbose mode: always report test log

-h               Show help

file             File to use as a script


Macro definitions that can be overridden.

varnishd         Path to varnishd to use [varnishd]

If `TMPDIR` is set in the environment, varnishtest creates temporary
`vtc.*` directories for each test in `$TMPDIR`, otherwise in `/tmp`.

SCRIPTS
=======

The script language used for Varnishtest is not a strictly defined
language. The best reference for writing scripts is the varnishtest program
itself. In the Varnish source code repository, under
`bin/varnishtest/tests/`, all the regression tests for Varnish are kept.

An example::

        varnishtest "#1029"

        server s1 {
                rxreq
                expect req.url == "/bar"
                txresp -gzipbody {[bar]}

                rxreq
                expect req.url == "/foo"
                txresp -body {<h1>FOO<esi:include src="/bar"/>BARF</h1>}

        } -start

        varnish v1 -vcl+backend {
                sub vcl_backend_response {
                        set beresp.do_esi = true;
                        if (bereq.url == "/foo") {
                                set beresp.ttl = 0s;
                        } else {
                                set beresp.ttl = 10m;
                        }
                }
        } -start

        client c1 {
                txreq -url "/bar" -hdr "Accept-Encoding: gzip"
                rxresp
                gunzip
                expect resp.bodylen == 5

                txreq -url "/foo" -hdr "Accept-Encoding: gzip"
                rxresp
                expect resp.bodylen == 21
        } -run

When run, the above script will simulate a server (s1) that expects two
different requests. It will start a Varnish server (v1) and add the backend
definition to the VCL specified (-vcl+backend). Finally it starts the
c1-client, which is a single client sending two requests.

SEE ALSO
========

* varnishtest source code repository with tests
* varnishhist(1)
* varnishlog(1)
* varnishncsa(1)
* varnishstat(1)
* varnishtop(1)
* vcl(7)

HISTORY
=======

The varnishtest program was developed by Poul-Henning Kamp
<phk@phk.freebsd.dk> in cooperation with Varnish Software AS.
This manual page was originally written by Stig Sandbeck Mathisen
<ssm@linpro.no> and updated by Kristian Lyngstøl
<kristian@varnish-cache.org>.


COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2007-2014 Varnish Software AS
