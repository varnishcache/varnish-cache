.. role:: ref(emphasis)

.. _varnishtest(1):

===========
varnishtest
===========

------------------------
Test program for Varnish
------------------------

:Manual section: 1

SYNOPSIS
========

varnishtest [-hikLlqvW] [-b size] [-D name=val] [-j jobs] [-n iter] [-t duration] file [file ...]

DESCRIPTION
===========

The varnishtest program is a script driven program used to test the
Varnish Cache.

The varnishtest program, when started and given one or more script
files, can create a number of threads representing backends, some
threads representing clients, and a varnishd process. This is then used to
simulate a transaction to provoke a specific behavior.

The following options are available:

-b size          Set internal buffer size (default: 512K)

-D name=val      Define macro for use in scripts

-h               Show help

-i               Set PATH and vmod_path to find varnish binaries in build tree

-j jobs          Run this many tests in parallel

-k               Continue on test failure

-L               Always leave temporary vtc.*

-l               Leave temporary vtc.* if test fails

-n iterations    Run tests this many times

-p name=val      Pass parameters to all varnishd command lines

-q               Quiet mode: report only failures

-t duration      Time tests out after this long

-v               Verbose mode: always report test log

-W               Enable the witness facility for locking

file             File to use as a script


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

TESTING A BUILD TREE
====================

Whether you are building a VMOD or trying to use one that you freshly built,
you can tell ``varnishtest`` to pass a *vmod_path* to ``varnishd`` instances
started using the ``varnish -start`` command in your test case::

    varnishtest -p vmod_path=... /path/to/*.vtc

This way you can use the same test cases on both installed and built VMODs::

    server s1 {...} -start

    varnish v1 -vcl+backend {
        import wossname;

        ...
    } -start

    ...

You are not limited to the *vmod_path* and can pass any parameter, allowing
you to run a build matrix without changing the test suite. You can achieve the
same with macros, but then they need to be defined on each run.

You can see the actual ``varnishd`` command lines in test outputs, they look
roughly like this::

    exec varnishd [varnishtest -p params] [testing params] [vtc -arg params]

Parameters you define with ``varnishtest -p`` may be overriden by parameters
needed by ``varnishtest`` to run properly, and they may in turn be overriden
by parameters set in test scripts.

There's also a special mode in which ``varnishtest`` builds itself a PATH and
a *vmod_path* in order to find Varnish binaries (programs and VMODs) in the
build tree surrounding the ``varnishtest`` binary. This is meant for testing
of Varnish under development and will disregard your *vmod_path* if you set
one.

If you need to test your VMOD against a Varnish build tree, you must install
it first, in a temp directory for instance. With information provided by the
installation's *pkg-config(1)* you can build a proper PATH in order to access
Varnish programs, and a *vmod_path* to access both your VMOD and the built-in
VMODs::

    export PKG_CONFIG_PATH=/path/to/install/lib/pkgconfig

    BINDIR="$(pkg-config --variable=bindir varnishapi)"
    SBINDIR="$(pkg-config --variable=sbindir varnishapi)"
    PATH="SBINDIR:BINDIR:$PATH"

    VMODDIR"$(pkg-config --variable=vmoddir varnishapi)"
    VMOD_PATH="/path/to/your/vmod/build/dir:$VMODDIR"

    varnishtest -p vmod_path="$VMOD_PATH" ...


AVAILABLE COMMANDS
==================

server
******

Creates a mock server that can accept requests from Varnish and send
responses. Accepted parameters:

\-listen
  specifies address and port to listen on (e.g. "127.0.0.1:80")

client
******

Creates a client instance that sends requests to Varnish and receives responses.
By default, a client will try and connect to the first varnish server available.

Accepted parameters:

\-connect
  specify where to connect to (e.g. "-connect ${s1_sock}").

server/client command arguments
*******************************

\-repeat INT
 repeats the commands INT in order
\-wait
 waits for commands to complete
\-start
 starts the client, and continue without waiting for completion
\-run
 equivalent to -start then -wait


varnish
*******

Starts Varnish instance. Accepted arguments:

\-arg STRING
 passes additional arguments to varnishd
\-cli
 executes a command in CLI of running instance
\-cliok
 executes a command and expect it return OK status
\-clierr
 executes a command and expect it to error with given status
 (e.g. "-clierr 300 panic.clear")
\-vcl STRING
 specify VCL for the instance. You can create multiline strings by encasing them
 in curly braces.
\-vcl+backend STRING
 specifes VCL for the instance, and automatically inject backends definition
 of currently defined servers.
\-errvcl
 tests that invalid VCL results in an error.
\-stop
 stops the instance
\-wait-stopped
 waits for the varnish child to stop
\-wait-running
 waits for the varnish child to start
\-wait
 waits for varnish to stop
\-expect
 sets up a test for asserting variables against expected results.
 Syntax: "-expect <var> <comparison> <const>"

See tests supplied with Varnish distribution for usage examples for all these
directives.

delay
*****
Sleeps for specified number of seconds. Can accept floating point numbers.

Usage: ``delay FLOAT``

varnishtest
***********

Accepts a string as an only argument. This being a test name that is being output
into the log. By default, test name is not shown, unless it fails.

shell
*****

Executes a shell command. Accepts one argument as a string, and runs the command
as is.

Usage: ``shell "CMD"``

sema
****

Semaphores mostly used to synchronize clients and servers "around"
varnish, so that the server will not send something particular
until the client tells it to, but it can also be used synchronize
multiple clients or servers running in parallel.

Usage: ``sema NAME sync INT``

NAME is of the form 'rX', X being a positive integer. This command blocks until,
in total, INT semaphores named NAME block.

random
******

Initializes random generator (need to call std.random() in vcl). See m00002.vtc
for more info

feature
*******

Checks for features to be present in the test environment. If feature is not present, test is skipped.

Usage: ``feature STRING [STRING...]``

Possible checks:

SO_RCVTIMEO_WORKS
 runs the test only if SO_RCVTIMEO option works in the environment
64bit
 runs the test only if environment is 64 bit
!OSX
 skips the test if ran on OSX
topbuild
 varnishtest has been started with '-i' and set the ${topbuild} macro.
logexpect
 This allows checking order and contents of VSL records in varnishtest.

SEE ALSO
========

* varnishtest source code repository with tests
* :ref:`varnishhist(1)`
* :ref:`varnishlog(1)`
* :ref:`varnishncsa(1)`
* :ref:`varnishstat(1)`
* :ref:`varnishtop(1)`
* :ref:`vcl(7)`

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

* Copyright (c) 2007-2016 Varnish Software AS
