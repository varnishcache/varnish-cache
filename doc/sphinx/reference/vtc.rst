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
