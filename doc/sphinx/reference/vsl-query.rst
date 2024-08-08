..
	Copyright (c) 2013-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. role:: ref(emphasis)

.. _vsl-query(7):

=========
vsl-query
=========

-----------------------------
Varnish VSL Query Expressions
-----------------------------

:Manual section: 7

OVERVIEW
========

The Varnish VSL Query Expressions extracts transactions from the
Varnish shared memory log, and perform queries on the transactions
before reporting matches.

A transaction is a set of log lines that belongs together, e.g. a
client request or a backend request. The API monitors the log, and
collects all log records that make up a transaction before reporting
on that transaction. Transactions can also be grouped, meaning backend
transactions are reported together with the client transaction that
initiated it.

A query is run on a group of transactions. A query expression is true
if there is a log record within the group that satisfies the
condition. It is false only if none of the log records satisfies the
condition. Query expressions can be combined using boolean functions.
In addition to log records, it is possible to query transaction ids
(vxid) in query.

GROUPING
========

When grouping transactions, there is a hierarchy structure showing
which transaction initiated what. The level increases by one on an
'initiated by' relation, so for example a backend transaction will
have one higher level than the client transaction that initiated it on
a cache miss. Request restart transactions don't get their level
increased to make it predictable.

Levels start counting at 1, except when using raw where it will always
be 0.

The grouping modes are:

* ``session``

  All transactions initiated by a client connection are reported
  together. Client connections are open ended when using HTTP
  keep-alives, so it is undefined when the session will be
  reported. If the transaction timeout period is exceeded an
  incomplete session will be reported. Non-transactional data (vxid
  == 0) is not reported.

* ``request``

  Transactions are grouped by request, where the set will include the
  request itself as well as any backend requests or ESI-subrequests.
  Session data and non-transactional data (vxid == 0) is not
  reported.

* ``vxid``

  Transactions are not grouped, so each vxid is reported in its
  entirety. Sessions, requests, ESI-requests and backend requests are
  all reported individually. Non-transactional data is not reported
  (vxid == 0). This is the default.

* ``raw``

  Every log record will make up a transaction of its own. All data,
  including non-transactional data will be reported.

Transaction Hierarchy
---------------------

Example transaction hierarchy using request grouping mode ::

  Lvl 1: Client request (cache miss)
    Lvl 2: Backend request
    Lvl 2: ESI subrequest (cache miss)
      Lvl 3: Backend request
      Lvl 3: Backend request (VCL restart)
      Lvl 3: ESI subrequest (cache miss)
        Lvl 4: Backend request
    Lvl 2: ESI subrequest (cache hit)

MEMORY USAGE
============

The API will use pointers to shared memory log data as long as
possible to keep memory usage at a minimum. But as the shared memory
log is a ring buffer, data will get overwritten eventually, so the API
creates local copies of referenced log data when varnishd comes close
to overwriting still unreported content.

This process avoids loss of log data in many scenarios, but it is not
failsafe: Overruns where varnishd "overtakes" the log reader process
in the ring buffer can still happen when API clients cannot keep up
reading and/or copying, for instance due to output blocking.

Though being unrelated to grouping in principle, copying of log data
is particularly relevant for session grouping together with long
lasting client connections - for this grouping, the logging API client
process is likely to consume relevant amounts of memory. As the vxid
grouping also logs (potentially long lasting) sessions, it is also
likely to require memory for copies of log entries, but far less than
session grouping.

QUERY LANGUAGE
==============

A query expression consists of record selection criteria, and
optionally an operator and a value to match against the selected
records. ::

  <record selection criteria> <operator> <operand>

Additionally, a query expression can occur on the transaction
itself rather than log records belonging to the transaction. ::

  vxid <numerical operator> <integer>

A ``vxid`` query allows you to directly target a specific transaction,
whose id can be obtained from an ``X-Varnish`` HTTP header, the
default "guru meditation" error page, or ``Begin`` and ``Link`` log
records.

A query must fit on a single line, but it is possible to pass multiple
queries at once, one query per line. Empty lines are ignored, and the
list of queries is treated as if the 'or' operator was used to combine
them.

For example this list of queries::

  # catch varnish errors
  *Error

  # catch backend errors
  BerespStatus >= 500

is identical to this query::

  (*Error) or (BerespStatus >= 500)

Comments can be used and will be ignored, they start with the ``'#'``
character, which may be more useful when the query is read from a file.

For very long queries that couldn't easily be split into multiple queries
it is possible to break them into multiple lines with a backslash preceding
an end of line.

For example this query::

  BerespStatus >= 500

is identical to this query::

  BerespStatus \
  >= \
  500

A backslash-newline sequence doesn't continue a comment on the next line
and isn't allowed in a quoted string.

Record selection criteria
-------------------------

The record selection criteria determines what kind records from the
transaction group the expression applies to. Syntax: ::

  {level}taglist:record-prefix[field]

Taglist is mandatory, the other components are optional.

The level limits the expression to a transaction at that level. If
left unspecified, the expression is applied to transactions at all
levels. Level is a positive integer or zero. If level is followed by a
'+' character, it expresses greater than or equal. If level is
followed by a '-', it expresses less than or equal.

The taglist is a comma-separated list of VSL record tags that this
expression should be checked against. Each list element can be a tag
name or a tag glob. Globs allow a '*' either in the beginning of
the name or at the end, and will select all tags that match either the
prefix or subscript. A single '*' will select all tags.

The record prefix will further limit the matches to those records that
has this prefix as their first part of the record content followed by a
colon. The part of the log record matched against will then be limited
to what follows the prefix and colon. This is useful when matching
against specific HTTP headers. The record prefix matching is done case
insensitive.

The field will, if present, treat the log record as a white space
separated list of fields, and only the nth part of the record will be
matched against. Fields start counting at 1.

An expression using only a record selection criteria will be true if
there is any record in the transaction group that is selected by the
criteria.

Operators
---------

The following matching operators are available:

* == != < <= > >=

  Numerical comparison. The record contents will be converted to
  either an integer or a float before comparison, depending on the
  type of the operand.

* eq ne

  String comparison. 'eq' tests string equality, 'ne' tests for not
  equality.

* ~ !~

  Regular expression matching. '~' is a positive match, '!~' is a
  non-match.

Operand
-------

The operand is the value the selected records will be matched
against.

An operand can be quoted or unquoted. Quotes can be either single or
double quotes, and for quoted operands a backslash can be used to
escape the quotes.

Unquoted operands can only consist of the following characters: ::

  a-z A-Z 0-9 + - _ . *

The following types of operands are available:

* Integer

  A number without any fractional part, valid for the numerical
  comparison operators. The integer type is used when the operand does
  not contain any period (.) nor exponent (e) characters. However if
  the record evaluates as a float, only its integral part is used for
  the comparison.

* Float

  A number with a fractional part, valid for the numerical comparison
  operators. The float type is used when the operand does contain a
  period (.) or exponent (e) character.

* String

  A sequence of characters, valid for the string equality operators.

* Regular expression

  A PCRE2 regular expression. Valid for the regular expression
  operators.

Boolean functions
-----------------

Query expressions can be linked together using boolean functions. The
following are available, in decreasing precedence:

* not <expr>

  Inverts the result of <expr>

* <expr1> and <expr2>

  True only if both expr1 and expr2 are true

* <expr1> or <expr2>

  True if either of expr1 or expr2 is true

Expressions can be grouped using parenthesis.

QUERY EXPRESSION EXAMPLES
=========================

* Transaction group contains a request URL that equals to "/foo" ::

    ReqURL eq "/foo"

* Transaction group contains a request cookie header ::

    ReqHeader:cookie

* Transaction group doesn't contain a request cookie header ::

    not ReqHeader:cookie

* Client request where internal handling took more than 800ms.::

    Timestamp:Process[2] > 0.8

* Transaction group contains a request user-agent header that contains
  "iPod" and the request delivery time exceeds 1 second ::

    ReqHeader:user-agent ~ "iPod" and Timestamp:Resp[2] > 1.

* Transaction group contains a backend response status larger than or
  equal to 500 ::

    BerespStatus >= 500

* Transaction group contains a request response status of 304, but
  where the request did not contain an if-modified-since header ::

    RespStatus == 304 and not ReqHeader:if-modified-since

* Transactions that have had backend failures or long delivery time on
  their ESI subrequests. (Assumes request grouping mode). ::

    BerespStatus >= 500 or {2+}Timestamp:Process[2] > 1.

* Log non-transactional errors. (Assumes raw grouping mode). ::

    vxid == 0 and Error

HISTORY
=======

This document was initially written by Martin Blix Grydeland and amended
by others.


COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2006 Verdens Gang AS
* Copyright (c) 2006-2015 Varnish Software AS
