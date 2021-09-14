..
	Copyright (c) 2016-2017 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. role:: ref(emphasis)

.. _vtc(7):

===
VTC
===

------------------------
Varnish Test Case Syntax
------------------------

:Manual section: 7

OVERVIEW
========

This document describes the syntax used by Varnish Test Cases files (.vtc).
A vtc file describe a scenario with different scripted HTTP-talking entities,
and generally one or more Varnish instances to test.

PARSING
=======

A vtc file will be read word after word, with very little tokenization, meaning
a syntax error won't be detected until the test actually reach the relevant
action in the test.

A parsing error will most of the time result in an assert being triggered. If
this happens, please refer yourself to the related source file and line
number. However, this guide should help you avoid the most common mistakes.

Words and strings
-----------------

The parser splits words by detecting whitespace characters and a string is a
word, or a series of words on the same line enclosed by double-quotes ("..."),
or, for multi-line strings, enclosed in curly brackets ({...}).

Comments
--------

The leading whitespaces of lines are ignored. Empty lines (or ones consisting
only of whitespaces) are ignored too, as are the lines starting with "#" that
are comments.

Lines and commands
------------------

Test files take at most one command per line, with the first word of the line
being the command and the following ones being its arguments. To continue over
to a new line without breaking the argument string, you can escape the newline
character (\\n) with a backslash (\\).

.. _vtc-macros:

MACROS
======

When a string is processed, macro expansion is performed. Macros are in the
form ``${<name>[,<args>...]}``, they have a name followed by an optional
comma- or space-separated list of arguments. Leading and trailing spaces are
ignored.

The macros ``${foo,bar,baz}`` and ``${ foo bar baz }`` are equivalent. If an
argument contains a space or a comma, arguments can be quoted. For example the
macro ``${foo,"bar,baz"}`` gives one argument ``bar,baz`` to the macro called
``foo``.

Unless documented otherwise, all macros are simple macros that don't take
arguments.

Built-in macros
---------------

``${bad_backend}``
	A socket address that will reliably never accept connections.

``${bad_ip}``
	An unlikely IPv4 address.

``${date}``
	The current date and time formatted for HTTP.

``${listen_addr}``
	The default listen address various components use, by default a random
	port on localhost.

``${localhost}``
	The first IP address that resolves to "localhost".

``${pwd}``
	The working directory from which ``varnishtest`` was executed.

``${string,<action>[,<args>...]}``
	The ``string`` macro is the entry point for text generation, it takes
	a specialized action with each its own set of arguments.

``${string,repeat,<uint>,<str>}``
	Repeat ``uint`` times the string ``str``.

``${testdir}``
	The directory containing the VTC script of the ongoing test case
	execution.

``${tmpdir}``
	The dedicated working directory for the ongoing test case execution,
	which happens to also be the current working directory. Useful when an
	absolute path to the working directory is needed.

``${topbuild}``
	Only present when the ``-i`` option is used, to work on Varnish itself
	instead of a regular installation.

SYNTAX
======

.. include:: ../include/vtc-syntax.rst

HISTORY
=======

This document has been written by Guillaume Quintard.

SEE ALSO
========

* :ref:`varnishtest(1)`
* :ref:`vmod_vtc(3)`

COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2006-2016 Varnish Software AS
