.. _ref-varnishlog:

==========
varnishlog
==========

--------------------
Display Varnish logs
--------------------

:Author: Dag-Erling Smørgrav
:Author: Per Buer
:Author: Martin Blix Grydeland
:Date:   2013-05-15
:Version: 0.3
:Manual section: 1


SYNOPSIS
========

varnishlog [-a] [-b] [-c] [-C] [-d] [-D] [-i tag] [-I [tag:]regex] [-k
keep] [-n varnish_name] [-P file] [-r file] [--raw] [-s num] [-S] [-u]
[-v] [-V] [-w file] [-x tag] [-X [tag:]regex] <query expression>

OPTIONS
=======

The following options are available:

.. include:: ../../../bin/varnishlog/varnishlog_options.rst

-b

	Only show backend transactions. If neither -b nor -c is
	specified, varnishlog acts as if they both were specified.

	XXX: Not yet implemented

-c

	Only show client transactions. If neither -b nor -c is
	specified, varnishlog acts as if they both were present.

	XXX: Not yet implemented

-C

	Ignore case when matching regular expressions.

	XXX: Not yet implemented

-D

	Daemonize.

	XXX: Not yet implemented

-I [tag:]regex

	Output only records matching this regular expression. If tag
	is given, limit the regex matching to records of that
	tag. Multiple -I options may be given.

	XXX: Not yet implemented

-k num

	Only show the first num log transactions (or log records
	in --raw mode)

	XXX: Not yet implemented

-P file

	Write the process' PID to the specified file.

	XXX: Not yet implemented

-s num

	Skip the first num log transactions (or log records if
	in --raw mode)

	XXX: Not yet implemented

-u

	Unbuffered output.

	XXX: Not yet implemented

-V

	Display the version number and exit.

	XXX: Not yet implemented


-X [tag:]regex

	Do not output log records matching this regex. If tag is
	given, limit the regex matching to records of that tag.
	Multiple -X options may be given.

	XXX: Not yet implemented


DESCRIPTION
===========

Varnishlog is a utility to extract and query the Varnish shared memory
log.

Varnishlog operates on transactions. A transaction is a set of log
lines that belongs together, e.g. a client request. Varnishlog will
monitor the log, and collect all log records that make up a
transaction before reporting on that transaction. Transactions can
also be grouped, meaning backend transactions are reported together
with the client transaction that initiated it.

The grouping levels are:

* Session

  All transactions initiated by a client connection is reported
  together. All log data is buffered until the client connection is
  closed.

* Request

  Transactions are grouped by request, where the set will include the
  request itself, and any backend requests or ESI-subrequests. Session
  data is not reported. This is the default.

* VXID

  Transactions are not grouped, so each VXID is reported in it's
  entirity. Sessions, requests, ESI-requests and backend requests are
  all reported individually. Non-transactional data is not reported
  (VXID == 0).

* Raw

  Every log record will make up a transaction of it's own. All data,
  including non-transactional data will be reported.


Grouping
========

When grouping transactions, there is a hirarchy structure showing
which transaction initiated what. The level increases by one by a
'initiated by' relation, so for example a backend transaction will
have one higher level than the client transaction that initiated it on
a cache miss.

Example transaction hirarchy ::

  Lvl 1: Client request (cache miss)
    Lvl 2: Backend request
    Lvl 2: ESI subrequest (cache miss)
      Lvl 3: Backend request
      Lvl 3: Backend request (VCL restart)
      Lvl 3: ESI subrequest (cache miss)
        Lvl 4: Backend request
    Lvl 2: ESI subrequest (cache hit)

Query operators will unless limited see a grouped set of transactions
together, and matching will be done on any log line from the complete
set. See QUERY LANGUAGE for how to limit a match to a specific part of
the set.

Running queries in session grouping mode can potentially consume a lot
of memory.


QUERY LANGUAGE
==============

XXX: As a POC only a single string is accepted as a query expression,
and this will be used as a regular expression that will be matched
against any log line of the set. The rest of the query language is yet
to be implemented.

The query expression is given as a single command line
argument. Additional arguments will give an error.

An expression consists of a single tag, or a comparison between a tag
and a constant.

A single tag expression is considered true if there is one or more
records with that tag in the transaction.

For all comparisons, the LHS must be a tag, and the RHS must be a
constant.

Constants must be quoted if they contain whitespace. You can use
either single or double quotes.

A comparison expression is true if the comparison is true for one or
more records with that tag in the transaction.

(be)?re(q|sp).(url|request|status|response) expands to their specific
tags.

(be)?re(q|sp).http.<header> expands to their corresponding
(Ber|R)(eq|esp)Header tag, and for this comparison the value will be
s/^(?i)<header>: //

<tag>{n} will only match on a transaction at the nth level (see
grouping). Levels starts counting at 0. If n is followed by a '+',
it will only match at level n or higher. If n is followed by a '-', it
will only match at level n or lower.

<tag>[n] will consider the value of the tag to be a white-space
separated field list, and extract the nth field for the comparison.

<tag>#n adds a repetition counter to this match, and is true only if
the match is true n times. n+ means n or more, n- means n or less.

'==', '!=', '<', '<=', '>' and '>=' are numerical comparisons. Integer
by default, or floating point if the RHS contains a dot. LHS will be
transformed (atoi/atof) for comparison.

'eq' and 'ne' are for string comparison.

'~' and '!~' are PCRE regular expression comparisons.

'not' is for negation

'and' is concatenation

'or' is alteration

'not' has highest precedence, alternation and concatenation have equal
precedence and associate left to right. Paranthesis can be used to
group statements.

QUERY EXAMPLES
==============

The following commands will list the entire client transaction of
requests where the url is "/foo" ::

	$ varnishlog -c 'req.url eq "/foo"'
	$ varnishlog -c 'ReqURL eq "/foo"'

The following command will list the URL of all requests that has a
cookie-header ::

	$ varnishlog -c -i ReqURL req.http.cookie
	$ varnishlog -c -i ReqURL 'ReqHeader ~ "^Cookie: "'

Report the User-Agent of logged in clients where the request delivery
time exceeds exceeds 0.5 seconds ::

	$ varnishlog -c -I RxHeader:User-Agent 'req.http.cookie ~
	"logged_in" and ReqEnd[5] > 0.5'

Report delivery status code of client requests that had one or more
503 errors from backend requests ::

	$ varnishlog -i TxStatus 'beresp.status == 503'

Report transaction set on requests that has backend failures
or long delivery time on their ESI subrequests ::

	$ varnishlog 'beresp.status{2+} >= 500 or ReqEnd{1+}[5] > 0.5'


TAGS
====
The following log entry tags are currently defined:

* Backend
* BackendClose
* BackendOpen
* BackendReuse
* BackendXID
* CLI
* ClientAddr
* Debug
* Error
* ExpBan
* ExpKill
* ExpPick
* Hit
* HitPass
* HttpError
* HttpGarbage
* Length
* ObjHeader
* ObjLostHeader
* ObjProtocol
* ObjRequest
* ObjResponse
* ObjStatus
* ObjURL
* ReqEnd
* ReqStart
* RxHeader
* RxLostHeader
* RxProtocol
* RxRequest
* RxResponse
* RxStatus
* RxURL
* SessionClose
* SessionOpen
* StatAddr
* StatSess
* TTL
* TxHeader
* TxLostHeader
* TxProtocol
* TxRequest
* TxResponse
* TxStatus
* TxURL
* VCL_acl
* VCL_call
* VCL_return
* VCL_trace
* WorkThread


SEE ALSO
========
* varnishd(1)
* varnishhist(1)
* varnishncsa(1)
* varnishstat(1)
* varnishtop(1)

HISTORY
=======

The varnishlog utility was developed by Poul-Henning Kamp
⟨phk@phk.freebsd.dk⟩ in cooperation with Verdens Gang AS, Varnish
Software AS and Varnish Software.  This manual page was initially
written by Dag-Erling Smørgrav.


COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2006 Verdens Gang AS
* Copyright (c) 2006-2013 Varnish Software AS
