..
	Copyright (c) 2010-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. role:: ref(emphasis)

.. _vcl(7):

===
VCL
===

------------------------------
Varnish Configuration Language
------------------------------

:Manual section: 7

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

Starting with Varnish 4.0, each VCL file must start by declaring its
version with ``vcl`` *<major>.<minor>*\ ``;`` marker at the top of
the file.  See more about this under Versioning below.


Operators
---------

The following operators are available in VCL:

  ``=``
    Assignment operator.

  ``+``, ``-``, ``*``, ``/``, ``%``
    Basic math on numerical values.

  ``+=``, ``-=``, ``*=``, ``/=``
    Assign and increment/decrement/multiply/divide operator.

    For strings, ``+=`` appends.

  ``(``, ``)``
    Evaluate separately.

  ``==``, ``!=``, ``<``, ``>``, ``<=``, ``>=``
    Comparisons

  ``~``, ``!~``
    Match / non-match. Can either be used with regular expressions or ACLs.

  ``!``
    Negation.

  ``&&`` / ``||``
    Logical and/or.


Conditionals
------------

VCL has ``if`` and ``else`` statements. Nested logic can be
implemented with the ``elseif`` statement (``elsif``\ /\ ``elif``\ /\
``else if`` are equivalent).

Note that there are no loops or iterators of any kind in VCL.

Variables
---------

VCL does most of the work by examining, ``set``'ing and ``unset``'ing
variables::

    if (req.url == "/mistyped_url.html") {
        set req.url = "/correct_url.html";
        unset req.http.cookie;
    }

There are obvious limitations to what can be done, for instance it
makes no sense to ``unset req.url;`` - a request must have some kind
of URL to be valid, and likewise trying to manipulate a backend response
when there is none (yet) makes no sense.
The VCL compiler will detect such errors.

Variables have types.  Most of them a STRINGS, and anything in
VCL can be turned into a STRING, but some variables have types like
``DURATION``, ``IP`` etc.

When setting a such variables, the right hand side of the equal
sign must have the correct variables type, you cannot assign a
STRING to a variable of type NUMBER, even if the string is ``"42"``.

Explicit conversion functions are available in :ref:`vmod_std(3)`.

For the complete album of VCL variables see: :ref:`vcl-var(7)`.


Strings
~~~~~~~

Basic strings are enclosed in double quotes ``"``\ *...*\ ``"``, and
may not contain newlines. Long strings are enclosed in
``{"``\ *...*\ ``"}`` or ``"""``\ *...*\ ``"""``. They may contain any
character including single double quotes ``"``, newline and other control
characters except for the *NUL* (0x00) character.

Booleans
~~~~~~~~

Booleans can be either ``true`` or ``false``.  In addition, in a boolean
context some data types will evaluate to ``true`` or ``false`` depending on
their value.

String types will evaluate to ``false`` if they are unset.  This allows
checks of the type ``if (req.http.opthdr) {}`` to test if a header
exists, even if it is empty, whereas ``if (req.http.opthdr == "") {}``
does not distinguish if the header does not exist or if it is empty.

Backend types
will evaluate to ``false`` if they don't have a backend assigned; integer
types will evaluate to ``false`` if their value is zero; duration types
will evaluate to ``false`` if their value is equal or less than zero.

Time
~~~~

VCL has time. A duration can be added to a time to make another time.
In string context they return a formatted string in RFC1123 format,
e.g. ``Sun, 06 Nov 1994 08:49:37 GMT``.

The keyword ``now`` returns a notion of the current time, which is
kept consistent during VCL subroutine invocations, so during the
execution of a VCL state subroutine (``vcl_* {}``), including all
user-defined subroutines being called, ``now`` always returns the
same value.

.. _vcl(7)_durations:

Durations
~~~~~~~~~

Durations are defined by a number followed by a unit. The number can
include a fractional part, e.g. ``1.5s``. The supported units are:

  ``ms``
    milliseconds

  ``s``
    seconds

  ``m``
    minutes

  ``h``
    hours

  ``d``
    days

  ``w``
    weeks

  ``y``
    years

In string context they return a string with their value rounded to
3 decimal places and excluding the unit, e.g.  ``1.500``.

Integers
~~~~~~~~

Certain fields are integers, used as expected. In string context they
return a string, e.g. ``1234``.

Real numbers
~~~~~~~~~~~~

VCL understands real numbers. In string context they return a string
with their value rounded to 3 decimal places, e.g. ``3.142``.

Regular Expressions
-------------------

Varnish uses Perl-compatible regular expressions (PCRE). For a
complete description please see the pcre(3) man page.

To send flags to the PCRE engine, such as to do case-insensitive matching, add
the flag within parens following a question mark, like this::

    # If host is NOT example dot com..
    if (req.http.host !~ "(?i)example\.com$") {
        ...
    }

.. _vcl-include:

Include statement
-----------------

To include a VCL file in another file use the ``include`` keyword::

    include "foo.vcl";

Optionally, the ``include`` keyword can take a ``+glob`` flag to include all
files matching a glob pattern::

    include +glob "example.org/*.vcl";

Import statement
----------------

The ``import`` statement is used to load Varnish Modules (VMODs.)

Example::

    import std;
    sub vcl_recv {
        std.log("foo");
    }

Comments
--------

Single lines of VCL can be commented out using ``//`` or
``#``. Multi-line blocks can be commented out with
``/*``\ *block*\ ``*/``.

Example::

    sub vcl_recv {
        // Single line of out-commented VCL.
        # Another way of commenting out a single line.
        /*
            Multi-line block of commented-out VCL.
        */
    }

Backends and health probes
--------------------------

Please see :ref:`vcl-backend(7)` and :ref:`vcl-probe(7)`

.. _vcl-acl:

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
enclosed in parentheses, however, it will simply be ignored if the
host name cannot be resolved.

To match an IP address against an ACL, simply use the match operator::

    if (client.ip ~ localnetwork) {
        return (pipe);
    }

ACLs have feature flags which can be set or cleared for each ACL
individually:

* `+log` - Emit a `Acl` record in VSL to tell if a match was found
  or not.

* `+table` - Implement the ACL with a table instead of compiled code.
  This runs a little bit slower, but compiles large ACLs much faster.

* `-pedantic` - Allow masks to cover non-zero host-bits.
  This allows the following to work::

    acl foo -pedantic +log {
        "firewall.example.com" / 24;
    }

  However, if the name resolves to both IPv4 and IPv6 you will still
  get an error.

* `+fold` - Fold ACL supernets and adjacent networks.

  With this parameter set to on, ACLs are optimized in that subnets
  contained in other entries are skipped (e.g.  if 1.2.3.0/24 is part
  of the ACL, an entry for 1.2.3.128/25 will not be added) and
  adjacent entries get folded (e.g.  if both 1.2.3.0/25 and
  1.2.3.128/25 are added, they will be folded to 1.2.3.0/24).

  Skip and fold operations on VCL entries are output as warnings
  during VCL compilation as entries from the VCL are processed in
  order.

  Logging under the ``VCL_acl`` tag can change with this parameter
  enabled: Matches on skipped subnet entries are now logged as matches
  on the respective supernet entry. Matches on folded entries are
  logged with a shorter netmask which might not be contained in the
  original ACL as defined in VCL. Such log entries are marked by
  ``fixed: folded``.

  Negated ACL entries are never folded.

VCL objects
-----------

A VCL object can be instantiated with the ``new`` keyword::

    sub vcl_init {
        new b = directors.round_robin()
        b.add_backend(node1);
    }

This is only available in ``vcl_init``.

Subroutines
-----------

A subroutine is used to group code for legibility or reusability::

    sub pipe_if_local {
        if (client.ip ~ localnetwork) {
            return (pipe);
        }
    }

Subroutines in VCL do not take arguments, nor do they return
values. The built in subroutines all have names beginning with ``vcl_``,
which is reserved.

To call a subroutine, use the ``call`` keyword followed by the
subroutine's name::

    sub vcl_recv {
        call pipe_if_local;
    }

Return statements
~~~~~~~~~~~~~~~~~

The ongoing ``vcl_*`` subroutine execution ends when a
``return(``\ *<action>*\ ``)`` statement is made.

The *<action>* specifies how execution should proceed. The context
defines which actions are available.

It is possible to exit a subroutine that is not part of the built-in ones
using a simple ``return`` statement without specifying an action. It exits
the subroutine without transitioning to a different state::

    sub filter_cookies {
        if (!req.http.cookie) {
            return;
        }
        # complex cookie filtering
    }

Multiple subroutines
~~~~~~~~~~~~~~~~~~~~

If multiple subroutines with the name of one of the built-in ones are defined,
they are concatenated in the order in which they appear in the source.

The built-in VCL distributed with Varnish will be implicitly concatenated
when the VCL is compiled.

Functions
---------

The following built-in functions are available:

.. _vcl(7)_ban:

ban(STRING)
~~~~~~~~~~~

  Deprecated. See :ref:`std.ban()`.

  The ``ban()`` function is identical to :ref:`std.ban()`, but does
  not provide error reporting.

hash_data(input)
~~~~~~~~~~~~~~~~

  Adds an input to the hash input. In the built-in VCL ``hash_data()``
  is called on the host and URL of the request. Available in ``vcl_hash``.

synthetic(STRING)
~~~~~~~~~~~~~~~~~

  Prepare a synthetic response body containing the *STRING*. Available
  in ``vcl_synth`` and ``vcl_backend_error``.

  Identical to ``set resp.body`` /  ``set beresp.body``.

.. list above comes from struct action_table[] in vcc_action.c.

regsub(str, regex, sub)
~~~~~~~~~~~~~~~~~~~~~~~

  Returns a copy of *str* with the first occurrence of the regular
  expression *regex* replaced with *sub*. Within *sub*, ``\0`` (which
  can also be spelled ``\&``) is replaced with the entire matched
  string, and ``\``\ *n* is replaced with the contents of subgroup *n*
  in the matched string.

regsuball(str, regex, sub)
~~~~~~~~~~~~~~~~~~~~~~~~~~
  As ``regsub()``, but this replaces all occurrences.

.. regsub* is in vcc_expr.c

For converting or casting VCL values between data types use the functions
available in the std VMOD.

Versioning
==========

Multiple versions of the VCL syntax can coexist within certain
constraints.

The VCL syntax version at the start of VCL file specified with ``-f``
sets the hard limit that cannot be exceeded anywhere, and it selects
the appropriate version of the builtin VCL.

That means that you can never include ``vcl 9.1;`` from ``vcl 8.7;``,
but the opposite *may* be possible, to the extent the compiler
supports it.

Files pulled in via ``include`` do not need to have a
``vcl`` *X.Y*\ ``;`` but it may be a good idea to do it anyway, to
not have surprises in the future.  The syntax version set in an
included file only applies to that file and any files it includes -
unless these set their own VCL syntax version.

The version of Varnish this file belongs to supports syntax 4.0 and 4.1.


EXAMPLES
========

For examples, please see the online documentation.

SEE ALSO
========

* :ref:`varnishd(1)`
* :ref:`vcl-backend(7)`
* :ref:`vcl-probe(7)`
* :ref:`vcl-step(7)`
* :ref:`vcl-var(7)`
* :ref:`vmod_directors(3)`
* :ref:`vmod_std(3)`

HISTORY
=======

VCL was developed by Poul-Henning Kamp in cooperation with Verdens
Gang AS, Redpill Linpro and Varnish Software.  This manual page is
written by Per Buer, Poul-Henning Kamp, Martin Blix Grydeland,
Kristian Lyngstøl, Lasse Karstensen and others.

COPYRIGHT
=========

This document is licensed under the same license as Varnish
itself. See LICENSE for details.

* Copyright (c) 2006 Verdens Gang AS
* Copyright (c) 2006-2015 Varnish Software AS
