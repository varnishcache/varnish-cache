..
	Copyright (c) 2011-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. role:: ref(emphasis)

.. _varnish-cli(7):

===========
varnish-cli
===========

------------------------------
Varnish Command Line Interface
------------------------------

:Manual section: 7

DESCRIPTION
===========

Varnish has a command line interface (CLI) which can control and change
most of the operational parameters and the configuration of Varnish,
without interrupting the running service.

The CLI can be used for the following tasks:

configuration
     You can upload, change and delete VCL files from the CLI.

parameters
     You can inspect and change the various parameters Varnish has
     available through the CLI. The individual parameters are
     documented in the varnishd(1) man page.

bans
     Bans are filters that are applied to keep Varnish from serving
     stale content. When you issue a ban Varnish will not serve any
     *banned* object from cache, but rather re-fetch it from its
     backend servers.

process management
     You can stop and start the cache (child) process though the
     CLI. You can also retrieve the latest stack trace if the child
     process has crashed.

If you invoke varnishd(1) with -T, -M or -d the CLI will be
available. In debug mode (-d) the CLI will be in the foreground, with
-T you can connect to it with varnishadm or telnet and with -M
varnishd will connect back to a listening service *pushing* the CLI to
that service. Please see :ref:`varnishd(1)` for details.

.. _ref_syntax:

Syntax
------

The Varnish CLI is similar to another command line interface, the Bourne
Shell. Commands are usually terminated with a newline, and they may take
arguments. The command and its arguments are *tokenized* before parsing,
and as such arguments containing spaces must be enclosed in double quotes.

It means that command parsing of

::

   help banner

is equivalent to

::

   "help" banner

because the double quotes only indicate the boundaries of the ``help``
token.

Within double quotes you can escape characters with \\ (backslash). The \\n,
\\r, and \\t get translated to newlines, carriage returns, an tabs.  Double
quotes and backslashes themselves can be escaped with \\" and \\\\
respectively.

To enter characters in octals use the \\nnn syntax. Hexadecimals can
be entered with the \\xnn syntax.

Commands may not end with a newline when a shell-style *here document*
(here-document or heredoc) is used. The format of a here document is::

   << word
	here document
   word

*word* can be any continuous string chosen to make sure it doesn't appear
naturally in the following *here document*. Traditionally EOF or END is
used.

Quoting pitfalls
----------------

Integrating with the Varnish CLI can be sometimes surprising when quoting
is involved. For instance in Bourne Shell the delimiter used with here
documents may or may not be separated by spaces from the ``<<`` token::

   cat <<EOF
   hello
   world
   EOF
   hello
   world

With the Varnish CLI, the ``<<`` and ``EOF`` tokens must be separated by
at least one blank::

   vcl.inline boot <<EOF
   106 258
   Message from VCC-compiler:
   VCL version declaration missing
   Update your VCL to Version 4 syntax, and add
           vcl 4.0;
   on the first line of the VCL files.
   ('<vcl.inline>' Line 1 Pos 1)
   <<EOF
   ##---

   Running VCC-compiler failed, exited with 2
   VCL compilation failed

With the missing space, the here document can be added and the actual VCL
can be loaded::

   vcl.inline test << EOF
   vcl 4.0;

   backend be {
           .host = "localhost";
   }
   EOF
   200 14
   VCL compiled.

A big difference with a shell here document is the handling of the ``<<``
token. Just like command names can be quoted, the here document token keeps
its meaning, even quoted::

   vcl.inline test "<<" EOF
   vcl 4.0;

   backend be {
           .host = "localhost";
   }
   EOF
   200 14
   VCL compiled.

When using a front-end to the Varnish-CLI like ``varnishadm``, one must
take into account the double expansion happening.  First in the shell
launching the ``varnishadm`` command and then in the Varnish CLI itself.
When a command's parameter require spaces, you need to ensure that the
Varnish CLI will see the double quotes::

   varnishadm param.set cc_command '"my alternate cc command"'

   Change will take effect when VCL script is reloaded

Otherwise if you don't quote the quotes, you may get a seemingly unrelated
error message::

   varnishadm param.set cc_command "my alternate cc command"
   Unknown request.
   Type 'help' for more info.
   Too many parameters

   Command failed with error code 105

If you are quoting with a here document, you must wrap it inside a shell
multi-line argument::

   varnishadm vcl.inline test '<< EOF
   vcl 4.0;

   backend be {
           .host = "localhost";
   }
   EOF'
   VCL compiled.

Another difference with a shell here document is that only one here document
can be used on a single command line. For example, it is possible to do this
in a shell script::

   #!/bin/sh

   cat << EOF1 ; cat << EOF2
   hello
   EOF1
   world
   EOF2

The expected output is::

   hello
   world

With the Varnish CLI, only the last parameter may use the here document form,
which greatly restricts the number of commands that can effectively use them.
Trying to use multiple here documents only takes the last one into account.

For example::

   command argument << EOF1 << EOF2
   heredoc1
   EOF1
   heredoc2
   EOF2

This conceptually results in the following command line:

- ``"command"``
- ``"argument"``
- ``"<<"``
- ``"EOF1"``
- ``"heredoc1\nEOF1\nheredoc2\n"``

Other pitfalls include variable expansion of the shell invoking ``varnishadm``
but this is not directly related to the Varnish CLI. If you get the quoting
right you should be fine even with complex commands.

JSON
----

A number of commands with informational responses support a ``-j`` parameter
for JSON output, as specified below. The top-level structure of the JSON
response is an array with these first three elements:

* A version number for the JSON format (integer)

* An array of strings that comprise the CLI command just received

* The time at which the response was generated, as a Unix epoch time
  in seconds with millisecond precision (floating point)

The remaining elements of the array form the data that are specific to
the CLI command, and their structure and content depend on the
command.

For example, the response to ``status -j`` just contains a string in
the top-level array indicating the state of the child process
(``"running"``, ``"stopped"`` and so forth)::

  [ 2, ["status", "-j"], 1538031732.632, "running"
  ]

The JSON responses to other commands may have longer lists of
elements, which may have simple data types or form structured objects.

JSON output is only returned if command execution was successful. The
output for an error response is always the same as it would have been
for the command without the ``-j`` parameter.

Commands
--------

.. include:: ../include/cli.rst

Backend Pattern
---------------

A backend pattern can be a backend name or a combination of a VCL name
and backend name in "VCL.backend" format.  If the VCL name is omitted,
the active VCL is assumed.  Partial matching on the backend and VCL
names is supported using shell-style wildcards, e.g. asterisk (*).

Examples::

   backend.list def*
   backend.list b*.def*
   backend.set_health default sick
   backend.set_health def* healthy
   backend.set_health * auto


Ban Expressions
---------------

A ban expression consists of one or more conditions.  A condition
consists of a field, an operator, and an argument.  Conditions can be
ANDed together with "&&".

A field can be any of the variables from VCL, for instance req.url,
req.http.host or obj.http.set-cookie.

Operators are "==" for direct comparison, "~" for a regular
expression match, and ">" or "<" for size comparisons.  Prepending
an operator with "!" negates the expression.

The argument could be a quoted string, a regexp, or an integer.
Integers can have "KB", "MB", "GB" or "TB" appended for size related
fields.


.. _ref_vcl_temperature:

VCL Temperature
---------------

A VCL program goes through several states related to the different
commands: it can be loaded, used, and later discarded. You can load
several VCL programs and switch at any time from one to another. There
is only one active VCL, but the previous active VCL will be maintained
active until all its transactions are over.

Over time, if you often refresh your VCL and keep the previous
versions around, resource consumption will increase, you can't escape
that. However, most of the time you want to pay the price only for the
active VCL and keep older VCLs in case you'd need to rollback to a
previous version.

The VCL temperature allows you to minimize the footprint of inactive
VCLs. Once a VCL becomes cold, Varnish will release all the resources
that can be be later reacquired. You can manually set the temperature
of a VCL or let varnish
automatically handle it.

EXAMPLES
========

Load a multi-line VCL using shell-style *here document*::

    vcl.inline example << EOF
    vcl 4.0;

    backend www {
        .host = "127.0.0.1";
        .port = "8080";
    }
    EOF

Ban all requests where req.url exactly matches the string /news::

    ban req.url == "/news"

Ban all documents where the serving host is "example.com" or
"www.example.com", and where the Set-Cookie header received from the
backend contains "USERID=1663"::

    ban req.http.host ~ "^(?i)(www\\.)?example\\.com$" && obj.http.set-cookie ~ "USERID=1663"

AUTHORS
=======

This manual page was originally written by Per Buer and later modified
by Federico G. Schwindt, Dridi Boukelmoune, Lasse Karstensen and
Poul-Henning Kamp.

SEE ALSO
========

* :ref:`varnishadm(1)`
* :ref:`varnishd(1)`
* :ref:`vcl(7)`
* For API use of the CLI: The Reference Manual.
