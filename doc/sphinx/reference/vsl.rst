.. _reference-vsl:

===
VSL
===

---------------------
Shared Memory Logging
---------------------

OVERVIEW
========

This document describes the format and content of all the Varnish shared memory
logging tags. These tags are used by the varnishlog(1), varnishtop(1), etc.
logging tools supplied with Varnish.

VSL tags
~~~~~~~~

.. include:: ../../../lib/libvarnishapi/vsl-tags.rst

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
VCL using the std.timestamp() method. If one is doing time consuming
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

Pipe handling timestamps
~~~~~~~~~~~~~~~~~~~~~~~~

Pipe
	Opened a pipe to the backend and forwarded the request.

PipeSess
	The pipe session has finished.

Backend fetch timestamps
~~~~~~~~~~~~~~~~~~~~~~~~

Start
	Start of the backend fetch processing.

Bereq
	Backend request sent.

Beresp
	Backend response headers received.

BerespBody
	Backend response body received.

Retry
	Backend request is being retried.

Error
	Backend request failed to vcl_backend_error.


HISTORY
=======

This document was initially written by Poul-Henning Kamp, and later updated by
Martin Blix Grydeland.


SEE ALSO
========
* varnishlog(1)
* varnishhist(1)
* varnishncsa(1)
* varnishtop(1)
