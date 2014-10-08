.. _reference-vcl:

===
VCL
===

------------------------------
Varnish Configuration Language
------------------------------

DESCRIPTION
===========

The VCL language is a small domain-specific language designed to be
used to describe request handling and document caching policies for
Varnish Cache.

When a new configuration is loaded, the varnishd management process
translates the VCL code to C and compiles it to a shared object which
is then loaded into the server process.

This document focuses on the syntax of the VCL language. For a full
description of syntax and semantics, with ample examples, please see
the online documentation at https://www.varnish-cache.org/docs/ .

Starting with Varnish 4.0, each VCL file must start by declaring its version
with a special "vcl 4.0;" marker at the top of the file.


Operators
---------

The following operators are available in VCL:

  =
    Assignment operator.

  ==
    Comparison.

  ~
    Match. Can either be used with regular expressions or ACLs.

  !
    Negation.

  &&
    Logical and.

  ||
    Logical or.


Conditionals
------------

VCL has *if* and *else* statements. Nested logic can be implemented
with the *elseif* statement. (*elsif*/*elif*/*else if* is equivalent.)

Note that are no loops or iterators of any kind in VCL.


Strings, booleans, time, duration and integers
----------------------------------------------

These are the data types in Varnish. You can *set* or *unset* these.

Example::

  set req.http.User-Agent = "unknown";
  unset req.http.Range;


Strings
~~~~~~~

Basic strings are enclosed in double quotes (" ... "), and may not contain
newlines. Long strings are enclosed in {" ... "}. They may contain any
character including single double quotes ("), newline and other control
characters except for the NUL (0x00) character.

Booleans
~~~~~~~~

Booleans can be either *true* or *false*.

Time
----

VCL has time. The function *now* returns a time. A duration can be
added to a time to make another time. In string context they return a
formatted string.

Durations
---------

Durations are defined by a number and a designation. The number can be a real
so 1.5w is allowed.

  ms
    milliseconds

  s
    seconds

  m
    minutes

  h
    hours

  d
    days

  w
    weeks

  y
    years

Integers
--------

Certain fields are integers, used as expected. In string context they
return a string.

Real numbers
------------

VCL understands real numbers. As with integers, when used in a string
context they will return a string.


Regular Expressions
-------------------

Varnish uses Perl-compatible regular expressions (PCRE). For a
complete description please see the pcre(3) man page.

To send flags to the PCRE engine, such as to do case insensitive matching, add
the flag within parens following a question mark, like this::

    # If host is NOT example dot com..
    if (req.http.host !~ "(?i)example.com$") {
        ...
    }


Include statement
-----------------

To include a VCL file in another file use the include keyword::

    include "foo.vcl";


Import statement
----------------

The *import* statement is used to load Varnish Modules (VMODs.)

Example::

    import std;
    sub vcl_recv {
        std.log("foo");
    }

Comments
--------

Single lines of VCL can be commented out using // or #. Multi-line blocks can
be commented out with \/\* block \/\*.

Example::

    sub vcl_recv {
        // Single line of out-commented VCL.
        # Another way of commenting out a single line.
        /*
            Multi-line block of commented-out VCL.
        */
    }


Backend definition
------------------

A backend declaration creates and initialises a named backend object. A
declaration start with the keyword *backend* followed by the name of the
backend. The actual declaration is in curly brackets, in a key/value fashion.::

    backend name {
        .attribute = "value";
    }

The only mandatory attribute is *host*. The attributes will inherit
their defaults from the global parameters. The following attributes
are available:

  host (mandatory)
    The host to be used. IP address or a hostname that resolves to a
    single IP address.

  port
    The port on the backend that Varnish should connect to.

  host_header
    A host header to add.

  connect_timeout
    Timeout for connections.

  first_byte_timeout
    Timeout for first byte.

  between_bytes_timeout
    Timeout between bytes.

  probe
    Attach a probe to the backend. See Probes.

  max_connections
    Maximum number of open connections towards this backend. If
    Varnish reaches the maximum Varnish it will start failing
    connections.

Backends can be used with *directors*. Please see the
vmod_directors(3) man page for more information.

.. _reference-vcl_probes:

Probes
------

Probes will query the backend for status on a regular basis and mark
the backend as down it they fail. A probe is defined as this:::

    probe name {
         .attribute = "value";
    }

There are no mandatory options. These are the options you can set:

  url
    The URL to query. Defaults to "/".

  request
    Specify a full HTTP request using multiple strings. .request will
    have \r\n automatically inserted after every string. If specified,
    .request will take precedence over .url.

  expected_response
    The expected HTTP response code. Defaults to 200.

  timeout
    The timeout for the probe. Default is 2s.

  interval
    How often the probe is run. Default is 5s.

  initial
    How many of the polls in .window are considered good when Varnish
    starts. Defaults to the value of threshold - 1. In this case, the
    backend starts as sick and requires one single poll to be
    considered healthy.

  window
    How many of the latest polls we examine to determine backend health.
    Defaults to 8.

  threshold
    How many of the polls in .window must have succeeded for us to
    consider the backend healthy. Defaults to 3.


Access Control List (ACL)
-------------------------

An Access Control List (ACL) declaration creates and initialises a named access
control list which can later be used to match client addresses::

    acl localnetwork {
        "localhost";    # myself
        "192.0.2.0"/24; # and everyone on the local network
        ! "192.0.2.23"; # except for the dial-in router
    }

If an ACL entry specifies a host name which Varnish is unable to
resolve, it will match any address it is compared to. Consequently,
if it is preceded by a negation mark, it will reject any address it is
compared to, which may not be what you intended. If the entry is
enclosed in parentheses, however, it will simply be ignored.

To match an IP address against an ACL, simply use the match operator::

    if (client.ip ~ localnetwork) {
        return (pipe);
    }


VCL objects
-----------

A VCL object can be made with the *new* keyword.

Example::

    sub vcl_init {
        new b = directors.round_robin()
        b.add_backend(node1);
    }


Subroutines
-----------

A subroutine is used to group code for legibility or reusability::

    sub pipe_if_local {
        if (client.ip ~ localnetwork) {
            return (pipe);
        }
    }

Subroutines in VCL do not take arguments, nor do they return
values. The built in subroutines all have names beginning with vcl\_,
which is reserved.

To call a subroutine, use the call keyword followed by the subroutine's name::

    sub vcl_recv {
        call pipe_if_local;
    }

Return statements
~~~~~~~~~~~~~~~~~

The ongoing vcl\_* subroutine execution ends when a return(*action*) statement
is made.

The *action* specifies how execution should proceed. The context defines
which actions are available.

Multiple subroutines
~~~~~~~~~~~~~~~~~~~~

If multiple subroutines with the name of one of the built-in ones are defined,
they are concatenated in the order in which they appear in the source.

The built-in VCL distributed with Varnish will be implicitly concatenated
when the VCL is compiled.


Variables
---------

In VCL you have access to certain variable objects. These contain
requests and responses currently being worked on. What variables are
available depends on context.

.. include:: ../include/vcl_var.rst


Functions
---------

The following built-in functions are available:

ban(expression)
  Invalidates all objects in cache that match the expression with the
  ban mechanism.

call(subroutine)
  Run a VCL subroutine within the current scope.

hash_data(input)
  Adds an input to the hash input. In the built-in VCL hash_data()
  is called on the host and URL of the *request*. Available in vcl_hash.

new()
  Instanciate a new VCL object. Available in vcl_init.

return()
  End execution of the current VCL subroutine, and continue to the next step
  in the request handling state machine.

rollback()
  Restore *req* HTTP headers to their original state. This function is
  deprecated.  Use std.rollback() instead.

synthetic(STRING)
  Prepare a synthetic response body containing the STRING. Available in
  vcl_synth and vcl_backend_error.

.. list above comes from struct action_table[] in vcc_action.c.

regsub(str, regex, sub)
  Returns a copy of str with the first occurrence of the regular
  expression regex replaced with sub. Within sub, \\0 (which can
  also be spelled \\&) is replaced with the entire matched string,
  and \\n is replaced with the contents of subgroup n in the
  matched string.

regsuball(str, regex, sub)
  As regsub() but this replaces all occurrences.

.. regsub* is in vcc_expr.c


EXAMPLES
========

For examples, please see the online documentation.

SEE ALSO
========

* varnishd(1)
* vmod_directors(3)
* vmod_std(3)

HISTORY
=======

VCL was developed by Poul-Henning Kamp in cooperation with Verdens
Gang AS, Redpill Linpro and Varnish Software.  This manual page is
written by Per Buer, Poul-Henning Kamp, Martin Blix Grydeland,
Kristian Lyngstøl, Lasse Karstensen and possibly others.

COPYRIGHT
=========

This document is licensed under the same license as Varnish
itself. See LICENSE for details.

* Copyright (c) 2006 Verdens Gang AS
* Copyright (c) 2006-2014 Varnish Software AS
