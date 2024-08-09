..
	Copyright (c) 2011-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. role:: ref(emphasis)

.. _vsl(7):

===
VSL
===

-----------------------------
Varnish Shared Memory Logging
-----------------------------

:Manual section: 7

OVERVIEW
========

This document describes the format and content of all the Varnish shared memory
logging tags. These tags are used by the varnishlog(1), varnishtop(1), etc.
logging tools supplied with Varnish.

VSL tags
~~~~~~~~

.. include:: ../include/vsl-tags.rst

TIMESTAMPS
==========

Timestamps are inserted in the log on completing certain events during
the worker thread's task handling. The timestamps has a label showing
which event was completed. The reported fields show the absolute time
of the event, the time spent since the start of the task and the time
spent since the last timestamp was logged.

The timestamps logged automatically by Varnish are inserted after
completing events that are expected to have delays (e.g. network IO or
spending time on a waitinglist). Timestamps can also be inserted from
VCL using the std.timestamp() function. If one is doing time consuming
tasks in the VCL configuration, it's a good idea to log a timestamp
after completing that task. This keeps the timing information in
subsequent timestamps from including the time spent on the VCL event.

Request handling timestamps
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Start
	The start of request processing (first byte received or
	restart).

Req
	Complete client request received.

ReqBody
	Client request body processed (discarded, cached or passed to
	the backend).

Waitinglist
	Came off waitinglist.

Fetch
	Fetch processing finished (completely fetched or ready for
	streaming).

Process
	Processing finished, ready to deliver the client response.

Resp
	Delivery of response to the client finished.

Restart
	Client request is being restarted.

Reset
        The client closed its connection, reset its stream or caused
        a stream error that forced Varnish to reset the stream. Request
        processing is interrupted and considered failed, with a 408
        "Request Timeout" status code.

Pipe handling timestamps
~~~~~~~~~~~~~~~~~~~~~~~~

The following timestamps are client timestamps specific to pipe transactions:

Pipe
	Opened a pipe to the backend and forwarded the request.

PipeSess
	The pipe session has finished.

The following timestamps change meaning in a pipe transaction:

Process
	Processing finished, ready to begin the pipe delivery.

Connect handling timestamps
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following timestamps are client timestamps specific to CONNECT
transactions:

Connect
	Opened a connection to the backend and forwarded the request.

ConnectSess
	The connection was closed.

The following timestamps change meaning in a CONNECT transaction:

Process
	Processing finished, ready to establish a tunnel.

Backend fetch timestamps
~~~~~~~~~~~~~~~~~~~~~~~~

Start
	Start of the backend fetch processing.

Fetch
	Came off vcl_backend_fetch ready to send the backend request.

Connected
	Successfully established a backend connection, or selected a recycled
	connection from the pool.

Bereq
	Backend request sent.

Beresp
	Backend response headers received.

Process
	Processing finished, ready to fetch the response body.

BerespBody
	Backend response body received.

Retry
	Backend request is being retried.

Error
	Backend request failed to vcl_backend_error.


NOTICE MESSAGES
===============

Notice messages contain informational messages about the handling of a
request. These can be exceptional circumstances encountered that causes
deviation from the normal handling. The messages are prefixed with ``vsl:``
for core Varnish generated messages, and VMOD authors are encouraged to
use ``vmod_<name>:`` for their own notice messages. This matches the name
of the manual page where detailed descriptions of notice messages are
expected.

The core messages are described below.

Conditional fetch wait for streaming object
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The backend answered 304 Not Modified on a conditional fetch using an object
that has not yet been fully fetched as the stale template object. This can
only happen when the TTL of the object is less than the time it takes to fetch
it. The fetch is halted until the stale object is fully fetched, upon which
the new object is created as normal. While waiting, any grace time on the
stale object will be in effect.

High number of variants
~~~~~~~~~~~~~~~~~~~~~~~

Objects are primarily looked up from an index using the hash key computed from
the ``hash_data()`` VCL function. When variants are involved, that is to say
when a backend response was stored with a ``Vary`` header, a secondary lookup
is performed but it is not indexed. As the number of variants for a given key
increases, this can slow cache lookups down, and since this happens under a
lock, this can greatly increase lock contention, even more so for frequently
requested objects. Variants should therefore be used sparingly on cacheable
responses, but since they can originate from either VCL or origin servers,
this notice should help identify problematic resources.


HISTORY
=======

This document was initially written by Poul-Henning Kamp, and later updated by
Martin Blix Grydeland.


SEE ALSO
========

* :ref:`varnishhist(1)`
* :ref:`varnishlog(1)`
* :ref:`varnishncsa(1)`
* :ref:`varnishtop(1)`

COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2006 Verdens Gang AS
* Copyright (c) 2006-2015 Varnish Software AS
