.. _ref-vsl-query:

=============================
Varnish VSL Query Expressions
=============================

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

GROUPING
========

When grouping transactions, there is a hierarchy structure showing
which transaction initiated what. The level increases by one by an
'initiated by' relation, so for example a backend transaction will
have one higher level than the client transaction that initiated it on
a cache miss. Request restart transactions does not have it's level
increased. This is to help predicting the level for a given
transaction.

Levels start counting at 1, except when using raw where it will always
be 0.

The grouping modes are:

* Session

  All transactions initiated by a client connection are reported
  together. All log data is buffered until the client connection is
  closed, which can cause session grouping mode to potentially consume
  a lot of memory.

* Request

  Transactions are grouped by request, where the set will include the
  request itself as well as any backend requests or ESI-subrequests.
  Session data is not reported. This is the default.

* VXID

  Transactions are not grouped, so each VXID is reported in it's
  entirety. Sessions, requests, ESI-requests and backend requests are
  all reported individually. Non-transactional data is not reported
  (VXID == 0).

* Raw

  Every log record will make up a transaction of it's own. All data,
  including non-transactional data will be reported.

Example transaction hierarchy ::

  Lvl 1: Client request (cache miss)
    Lvl 2: Backend request
    Lvl 2: ESI subrequest (cache miss)
      Lvl 3: Backend request
      Lvl 3: Backend request (VCL restart)
      Lvl 3: ESI subrequest (cache miss)
        Lvl 4: Backend request
    Lvl 2: ESI subrequest (cache hit)

QUERY LANGUAGE
==============

A query expression consists of record selection criteria, and
optionally an operator and a value to match against the selected
records. ::

  <record selection criteria> <operator> <operand>

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
has this prefix as it's first part of the record content followed by a
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
  not contain any period (.) characters.

* Float

  A number with a fractional part, valid for the numerical comparison
  operators. The float type is used when the operand does contain a
  period (.) character.

* String

  A sequence of characters, valid for the string equality operators.

* Regular expression

  A PCRE regular expression. Valid for the regular expression
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

* Transaction group contains a request user-agent header that contains
  "iPod" and the request delivery time exceeds 1 second ::

    ReqHeader:user-agent ~ "iPod" and ReqEnd[5] > 1.

* Transaction group contains a backend response status larger than or
  equal to 500 ::

    BerespStatus >= 500

* Transaction group contains a request response status of 304, but
  where the request did not contain an if-modified-since header ::

    ReqStatus == 304 and not ReqHeader:if-modified-since

* Transactions that have had backend failures or long delivery time on
  their ESI subrequests. (Assumes request grouping mode). ::

    BerespStatus >= 500 or {2+}ReqEnd[5] > 1.

HISTORY
=======

This document was written by Martin Blix Grydeland.

