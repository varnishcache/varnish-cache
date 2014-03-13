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
used to define request handling and document caching policies for
Varnish Cache.

When a new configuration is loaded, the varnishd management process
translates the VCL code to C and compiles it to a shared object which
is then dynamically linked into the server process.

This document focuses on the syntax of the VCL language. Full a full
description of syntax and semantics, with ample examples, please see
the users guide at https://www.varnish-cache.org/doc/

VCL consists of the following elements:
 * Operators
 * Conditionals
 * Strings, booleans, time, duration, ints
 * Regular expressions

In addition VCL has the following contructs:
 * Include
 * Backend definitions
 * Probes
 * Access control lists - ACLs
 * Import statement
 * Functions
 * Subroutines

Note that are no loops or iterators of any kind in VCL.

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
    Logical and

  ||
    Logical or


Conditionals
------------

VCL has *if* statments.


Strings, booleans, time, duration and ints
------------------------------------------

These are the data types in Varnish. You can *set* or *unset* these.

Example::

  set req.http.user-agent = "unknown";


Strings
~~~~~~~

Basic strings are enclosed in " ... ", and may not contain
newlines. Long strings are enclosed in {" ... "}. They may contain any
character including ", newline and other control characters except for
the NUL (0x00) character

Booleans
~~~~~~~~

Booleans can be either true or false.

Time
----

VCL has time. The function *now* returns a time. A duration can be
added to a time to make another time. In string context they return a
formatted string.

Durations
---------

Durations are defined by a number and a designation. The number can be a real so 1.5w is allowed.

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


Ints
----

Certain fields are integers, used as expected. In string context they
return a string.

Reals
-----

VCL understands real numbers. As with integers, when used in a string
context they will return a string.


Regular Expressions
-------------------

Varnish uses PCRE - Perl-compatible regular expressions. For a
complete description of PCRE please see the pcre(3) man page.

To send flags to the PCRE engine, such as to turn on *case insensitivity* 
add the flag within parens following a question mark,
like this::

    # If host is NOT example dot com..
    if (req.http.host !~ "(?i)example.com$") {
        ...
    }


Include statement
-----------------

To include a VCL file in another file use the include keyword::

  include "foo.vcl";


Backend definition
------------------

A backend declaration creates and initializes a named backend
object. A declaration start with the keyword *backend* followed by the
name of the backend. The actual declaration is in curly brackets, in a
key/value fashion.::

    backend name {
        .attribute = "value";
    }

The only mandatory attribute is host. The attributes will inherit
their defaults from the global parameters. The following attributes
are availble:

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
    The timeout for the probe. Default it 2s.

  interval
    How often the probe is run. Default is 5s.

  initial
    How many of the polls in .window are considered good when Varnish
    starts. Defaults to the value of threshold - 1. In this case, the
    backend starts as sick and requires one single poll to be
    conqsidered healthy.
            
  window
    How many of the latest polls we examine to determine backend health. Defaults to 8.
            
  threshold
    How many of the polls in .window must have succeeded for us to
    consider the backend healthy. If this is set to more than or equal
    to the threshold, the backend starts as healthy. Defaults to the
    value of threshold - 1. In this case, the backend starts as sick
    and requires one poll to pass to become healthy. Defaults to
    threshold - 1.


ACLs
----

An ACL declaration creates and initializes a named access control list
which can later be used to match client addresses::

    acl local {
        "localhost";    # myself
        "192.0.2.0"/24; # and everyone on the local network
        ! "192.0.2.23"; # except for the dialin router
    }

If an ACL entry specifies a host name which Varnish is unable to
resolve, it will match any address it is compared to.  Consequently,
if it is preceded by a negation mark, it will reject any address it is
compared to, which may not be what you intended.  If the entry is
enclosed in parentheses, however, it will simply be ignored.

To match an IP address against an ACL, simply use the match operator::

    if (client.ip ~ local) {
        return (pipe);
    }


Subroutines
-----------

A subroutine is used to group code for legibility or reusability::

    sub pipe_if_local {
        if (client.ip ~ local) {
            return (pipe);
        }
    }

Subroutines in VCL do not take arguments, nor do they return
values. The built in subroutines all have names beginning with vcl_,
which is reserved.

To call a subroutine, use the call keyword followed by the subroutine's name::

    call pipe_if_local;

Return statements
~~~~~~~~~~~~~~~~~

The subroutine executions ends when a return(*action*) statement is
made. The *action* specifies how execution should proceed. The context
defines which actions are availble. See the user guide for information
on what actions are available where.

Multiple subroutines
~~~~~~~~~~~~~~~~~~~~

If multiple subroutines with the name of one of the builtin
ones are defined, they are concatenated in the order in which they
appear in the source.

The default versions distributed with Varnish will be implicitly
concatenated.



Variables
---------

In VCL you have access to certain variable objects. These contain
requests and responses currently beeing worked on. What variables are
availble depends on context.

.. include:: vcl_var.rst


Functions
---------

The following built-in functions are available:

ban(expression)
  Bans all objects in cache that match the expression.

hash_data(str)
  Adds a string to the hash input. In the built-in VCL hash_data()
  is called on the host and URL of the *request*.

regsub(str, regex, sub)
  Returns a copy of str with the first occurrence of the regular
  expression regex replaced with sub. Within sub, \\0 (which can
  also be spelled \\&) is replaced with the entire matched string,
  and \\n is replaced with the contents of subgroup n in the
  matched string.

regsuball(str, regex, sub)
  As regsub() but this replaces all occurrences.


EXAMPLES
========

For examples, please see the guide guide.

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
Kristian Lyngstøl and possibly others.

COPYRIGHT
=========

This document is licensed under the same license as Varnish
itself. See LICENSE for details.

* Copyright (c) 2006 Verdens Gang AS
* Copyright (c) 2006-2014 Varnish Software AS
