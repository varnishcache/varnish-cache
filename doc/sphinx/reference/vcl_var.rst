..
	Copyright (c) 2018-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. NOTE: please maintain lexicographic order of foo.* variable names

.. _vcl_variables:


local, server, remote and client
--------------------------------

These variables describe the network connection between the
client and varnishd.

Without PROXY protocol::

	     client    server
	     remote    local
	       v          v
	CLIENT ------------ VARNISHD


With PROXY protocol::

	     client    server   remote     local
	       v          v       v          v
	CLIENT ------------ PROXY ------------ VARNISHD


.. _client.identity:

client.identity

	Type: STRING

	Readable from: client, backend

	Writable from: client


	Identification of the client, used to load balance
	in the client director.  Defaults to ``client.ip``

	This variable can be overwritten with more precise
	information, for instance extracted from a ``Cookie:``
	header.


.. _client.ip:

client.ip

	Type: IP

	Readable from: client, backend


	The client's IP address, either the same as ``remote.ip``
	or what the PROXY protocol told us.


.. _server.hostname:

server.hostname

	Type: STRING

	Readable from: all

	The host name of the server, as returned by the
	`gethostname(3)` system function.


.. _server.identity:

server.identity

	Type: STRING

	Readable from: all

	The identity of the server, as set by the ``-i`` parameter.

	If an ``-i`` parameter is not passed to varnishd, the return
	value from `gethostname(3)` system function will be used.


.. _server.ip:

server.ip

	Type: IP

	Readable from: client, backend


	The IP address of the socket on which the client
	connection was received, either the same as ``server.ip``
	or what the PROXY protocol told us.


.. _remote.ip:

remote.ip

	Type: IP

	Readable from: client, backend

	The IP address of the other end of the TCP connection.
	This can either be the clients IP, or the outgoing IP
	of a proxy server.

	If the connection is a UNIX domain socket, the value
	will be ``0.0.0.0:0``


.. _local.endpoint:

local.endpoint	``VCL >= 4.1``

	Type: STRING

	Readable from: client, backend

	The address of the '-a' socket the session was accepted on.

	If the argument was ``-a foo=:81`` this would be ":81"


.. _local.ip:

local.ip

	Type: IP

	Readable from: client, backend

	The IP address (and port number) of the local end of the
	TCP connection, for instance ``192.168.1.1:81``

	If the connection is a UNIX domain socket, the value
	will be ``0.0.0.0:0``


.. _local.socket:

local.socket	``VCL >= 4.1``

	Type: STRING

	Readable from: client, backend

	The name of the '-a' socket the session was accepted on.

	If the argument was ``-a foo=:81`` this would be "foo".

	Note that all '-a' gets a default name on the form ``a%d``
	if no name is provided.


req and req_top
---------------

These variables describe the present request, and when ESI:include
requests are being processed, req_top points to the request received
from the client.

.. _req:

req

	Type: HTTP

	Readable from: client


	The entire request HTTP data structure.
	Mostly useful for passing to VMODs.


.. _req.backend_hint:

req.backend_hint

	Type: BACKEND

	Readable from: client

	Writable from: client

	Set bereq.backend to this if we attempt to fetch.
	When set to a director, reading this variable returns
	an actual backend if the director has resolved immediately,
	or the director otherwise.
	When used in string context, returns the name of the director
	or backend, respectively.


.. _req.can_gzip:

req.can_gzip

	Type: BOOL

	Readable from: client

	True if the client provided ``gzip`` or ``x-gzip`` in the
	``Accept-Encoding`` header.


req.esi	``VCL <= 4.0``

	Type: BOOL

	Readable from: client

	Writable from: client

	Set to ``false`` to disable ESI processing
	regardless of any value in beresp.do_esi. Defaults
	to ``true``. This variable is replaced by ``resp.do_esi``
	in VCL 4.1.


.. _req.esi_level:

req.esi_level

	Type: INT

	Readable from: client

	A count of how many levels of ESI requests we're currently at.


.. _req.grace:

req.grace

	Type: DURATION

	Readable from: client

	Writable from: client


	Upper limit on the object grace.

	During lookup the minimum of req.grace and the object's stored
	grace value will be used as the object's grace.


.. _req.hash:

req.hash

	Type: BLOB

	Readable from: vcl_hit, vcl_miss, vcl_pass, vcl_purge, vcl_deliver


	The hash key of this request.
	Mostly useful for passing to VMODs, but can also be useful
	for debugging hit/miss status.


.. _req.hash_always_miss:

req.hash_always_miss

	Type: BOOL

	Readable from: client

	Writable from: client

	Default: ``false``.

	Force a cache miss for this request, even if perfectly
	good matching objects are in the cache.

	This is useful to force-update the cache without invalidating
	existing entries in case the fetch fails.


.. _req.hash_ignore_busy:

req.hash_ignore_busy

	Type: BOOL

	Readable from: client

	Writable from: client

	Default: ``false``.

	Ignore any busy object during cache lookup.

	You only want to do this when you have two server looking
	up content sideways from each other to avoid deadlocks.


.. _req.hash_ignore_vary:

req.hash_ignore_vary

	Type: BOOL

	Readable from: client

	Writable from: client

	Default: ``false``.

	Ignore objects vary headers during cache lookup.

	This returns the very first match regardless of the object
	compatibility with the client request. This is useful when
	variants are irrelevant to certain clients, and differences
	in the way the resource is presented don't change how the
	client will interpret it.

	Use with caution.


.. _req.http:

req.http.*

	Type: HEADER

	Readable from: client

	Writable from: client

	Unsettable from: client


	The headers of request, things like ``req.http.date``.

	The RFCs allow multiple headers with the same name, and both
	``set`` and ``unset`` will remove *all* headers with the name
	given.

	The header name ``*`` is a VCL symbol and as such cannot, for
	example, start with a numeral. To work with valid header that
	can't be represented as VCL symbols it is possible to quote the
	name, like ``req.http."grammatically.valid"``. None of the HTTP
	headers present in IANA registries need to be quoted, so the
	quoted syntax is discouraged but available for interoperability.

	Some headers that cannot be tampered with for proper HTTP fetch
	or delivery are read-only.


req.http.content-length

	Type: HEADER

	Readable from: client

	The content-length header field is protected, see protected_headers_.


req.http.transfer-encoding

	Type: HEADER

	Readable from: client

	The transfer-encoding header field is protected, see protected_headers_.


.. _req.is_hitmiss:

req.is_hitmiss

	Type: BOOL

	Readable from: client

	If this request resulted in a hitmiss


.. _req.is_hitpass:

req.is_hitpass

	Type: BOOL

	Readable from: client

	If this request resulted in a hitpass


.. _req.method:

req.method

	Type: STRING

	Readable from: client

	Writable from: client


	The request method (e.g. "GET", "HEAD", ...)


req.proto	``VCL <= 4.0``

	Type: STRING

	Readable from: client

	Writable from: client

	The HTTP protocol version used by the client, usually "HTTP/1.1"
	or "HTTP/2.0".

.. _req.proto:

req.proto	``VCL >= 4.1``

	Type: STRING

	Readable from: client

	The HTTP protocol version used by the client, usually "HTTP/1.1"
	or "HTTP/2.0".


.. _req.restarts:

req.restarts

	Type: INT

	Readable from: client


	A count of how many times this request has been restarted.


.. _req.storage:

req.storage

	Type: STEVEDORE

	Readable from: client

	Writable from: client


	The storage backend to use to save this request body.


.. _req.time:

req.time

	Type: TIME

	Readable from: client

	The time when the request was fully received, remains constant
	across restarts.


.. _req.trace:

req.trace

	Type: BOOL

	Readable from: client

	Writable from: client

	Controls if ``VCL_trace`` VSL records are emitted for the current
	request, see :ref:`vsl(7)`.

	Defaults to the setting of the ``feature trace`` parameter,
	see :ref:`varnishd(1)`. Does not get reset by a rollback.


.. _req.transport:

req.transport

	Type: STRING

	Readable from: client

	The transport protocol which brought this request.


.. _req.ttl:

req.ttl

	Type: DURATION

	Readable from: client

	Writable from: client


	Upper limit on the object age for cache lookups to return hit.


.. _req.url:

req.url

	Type: STRING

	Readable from: client

	Writable from: client


	The requested URL, for instance "/robots.txt".


.. _req.xid:

req.xid

	Type: INT

	Readable from: client

	Unique ID of this request.


.. _req_top.http:

req_top.http.*

	Type: HEADER

	Readable from: client

	HTTP headers of the top-level request in a tree of ESI requests.
	Identical to req.http. in non-ESI requests.

	See req.http_ for general notes.


.. _req_top.method:

req_top.method

	Type: STRING

	Readable from: client

	The request method of the top-level request in a tree
	of ESI requests. (e.g. "GET", "HEAD").
	Identical to req.method in non-ESI requests.


.. _req_top.proto:

req_top.proto

	Type: STRING

	Readable from: client

	HTTP protocol version of the top-level request in a tree of
	ESI requests.
	Identical to req.proto in non-ESI requests.


.. _req_top.time:

req_top.time

	Type: TIME

	Readable from: client

	The time when the top-level request was fully received,
	remains constant across restarts.


.. _req_top.url:

req_top.url

	Type: STRING

	Readable from: client

	The requested URL of the top-level request in a tree
	of ESI requests.
	Identical to req.url in non-ESI requests.


bereq
-----

This is the request we send to the backend, it is built from the
clients ``req.*`` fields by filtering out "per-hop" fields which
should not be passed along (``Connection:``, ``Range:`` and similar).

Slightly more fields are allowed through for ``pass` fetches
than for `miss` fetches, for instance ``Range``.

bereq

	Type: HTTP

	Readable from: backend

	The entire backend request HTTP data structure.
	Mostly useful as argument to VMODs.


.. _bereq.backend:

bereq.backend

	Type: BACKEND

	Readable from: vcl_pipe, backend

	Writable from: vcl_pipe, backend

	This is the backend or director we attempt to fetch from.
	When set to a director, reading this variable returns
	an actual backend if the director has resolved immediately,
	or the director otherwise.
	When used in string context, returns the name of the director
	or backend, respectively.


.. _bereq.between_bytes_timeout:

bereq.between_bytes_timeout

	Type: DURATION

	Readable from: backend

	Writable from: backend

	Unsettable from: vcl_pipe, backend

	Default: ``.between_bytes_timeout`` attribute from the
	:ref:`backend_definition`, which defaults to the
	``between_bytes_timeout`` parameter, see :ref:`varnishd(1)`.

	The time in seconds to wait between each received byte from the
	backend.  Not available in pipe mode.


.. _bereq.body:

bereq.body

	Type: BODY

	Unsettable from: vcl_backend_fetch

	The request body.

	Unset will also remove bereq.http.content-length_.

.. _bereq.connect_timeout:

bereq.connect_timeout

	Type: DURATION

	Readable from: vcl_pipe, backend

	Writable from: vcl_pipe, backend

	Unsettable from: vcl_pipe, backend

	Default: ``.connect_timeout`` attribute from the
	:ref:`backend_definition`, which defaults to the
	``connect_timeout`` parameter, see :ref:`varnishd(1)`.

	The time in seconds to wait for a backend connection to be
	established.


.. _bereq.first_byte_timeout:

bereq.first_byte_timeout

	Type: DURATION

	Readable from: backend

	Writable from: backend

	Unsettable from: vcl_pipe, backend

	Default: ``.first_byte_timeout`` attribute from the
	:ref:`backend_definition`, which defaults to the
	``first_byte_timeout`` parameter, see :ref:`varnishd(1)`.

	The time in seconds to wait getting the first byte back
	from the backend.  Not available in pipe mode.


.. _bereq.hash:

bereq.hash

	Type: BLOB

	Readable from: vcl_pipe, backend

	The hash key of this request, a copy of ``req.hash``.


.. _bereq.http:

bereq.http.*

	Type: HEADER

	Readable from: vcl_pipe, backend

	Writable from: vcl_pipe, backend

	Unsettable from: vcl_pipe, backend

	The headers to be sent to the backend.

	See req.http_ for general notes.


.. _bereq.http.content-length:

bereq.http.content-length

	Type: HEADER

	Readable from: backend

	The content-length header field is protected, see protected_headers_.


bereq.http.transfer-encoding

	Type: HEADER

	Readable from: backend

	The transfer-encoding header field is protected, see protected_headers_.


.. _bereq.is_bgfetch:

bereq.is_bgfetch

	Type: BOOL

	Readable from: backend

	True for fetches where the client got a hit on an object in
	grace, and this fetch was kicked of in the background to get
	a fresh copy.


.. _bereq.is_hitmiss:

bereq.is_hitmiss

	Type: BOOL

	Readable from: backend

	If this backend request was caused by a hitmiss.


.. _bereq.is_hitpass:

bereq.is_hitpass

	Type: BOOL

	Readable from: backend

	If this backend request was caused by a hitpass.


.. _bereq.method:

bereq.method

	Type: STRING

	Readable from: vcl_pipe, backend

	Writable from: vcl_pipe, backend

	The request type (e.g. "GET", "HEAD").

	Regular (non-pipe, non-pass) fetches are always "GET"


bereq.proto	``VCL <= 4.0``

	Type: STRING

	Readable from: vcl_pipe, backend

	Writable from: vcl_pipe, backend

	The HTTP protocol version, "HTTP/1.1" unless a pass or pipe
	request has "HTTP/1.0" in ``req.proto``

.. _bereq.proto:

bereq.proto	``VCL >= 4.1``

	Type: STRING

	Readable from: vcl_pipe, backend

	The HTTP protocol version, "HTTP/1.1" unless a pass or pipe
	request has "HTTP/1.0" in ``req.proto``


.. _bereq.retries:

bereq.retries

	Type: INT

	Readable from: backend

	A count of how many times this request has been retried.


.. _bereq.task_deadline:

bereq.task_deadline

	Type: DURATION

	Readable from: vcl_pipe

	Writable from: vcl_pipe

	Unsettable from: vcl_pipe

	Deadline for pipe sessions, defaults ``0s``, which falls back to the
	``pipe_task_deadline`` parameter, see :ref:`varnishd(1)`


.. _bereq.time:

bereq.time

	Type: TIME

	Readable from: vcl_pipe, backend

	The time when we started preparing the first backend request,
	remains constant across retries.


.. _bereq.trace:

bereq.trace

	Type: BOOL

	Readable from: backend

	Writable from: backend

	Controls if ``VCL_trace`` VSL records are emitted for the current
	request, see :ref:`vsl(7)`.

	Inherits the value of ``req.trace`` when the backend request
	is created. Does not get reset by a rollback.


.. _bereq.uncacheable:

bereq.uncacheable

	Type: BOOL

	Readable from: backend


	Indicates whether this request is uncacheable due to a
	`pass` in the client side or a hit on an hit-for-pass object.


.. _bereq.url:

bereq.url

	Type: STRING

	Readable from: vcl_pipe, backend

	Writable from: vcl_pipe, backend

	The requested URL, copied from ``req.url``


.. _bereq.xid:

bereq.xid

	Type: INT

	Readable from: vcl_pipe, backend

	Unique ID of this request.


beresp
------

The response received from the backend, one cache misses, the
store object is built from ``beresp``.

beresp

	Type: HTTP

	Readable from: vcl_backend_response, vcl_backend_error

	The entire backend response HTTP data structure, useful as
	argument to VMOD functions.

.. _beresp.age:

beresp.age

	Type: DURATION

	Readable from: vcl_backend_response, vcl_backend_error

	Default: Age header, or zero.

	The age of the object.


.. _beresp.backend:

beresp.backend

	Type: BACKEND

	Readable from: vcl_backend_response, vcl_backend_error

	This is the backend we fetched from.  If bereq.backend
	was set to a director, this will be the backend selected
	by the director.
	When used in string context, returns its name.


beresp.backend.ip	``VCL <= 4.0``

	Type: IP

	Readable from: vcl_backend_response

	IP of the backend this response was fetched from.


.. _beresp.backend.name:

beresp.backend.name

	Type: STRING

	Readable from: vcl_backend_response, vcl_backend_error

	Name of the backend this response was fetched from.
	Same as beresp.backend.


.. _beresp.body:

beresp.body

	Type: BODY

	Writable from: vcl_backend_error

	For producing a synthetic body.


.. _beresp.do_esi:

beresp.do_esi

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Default: ``false``.

	Set it to true to parse the object for ESI directives. This is
	necessary for later ESI processing on the client side. If
	beresp.do_esi is false when an object enters the cache, client
	side ESI processing will not be possible (obj.can_esi will be
	false).

	It is a VCL error to use beresp.do_esi after setting beresp.filters.


.. _beresp.do_gunzip:

beresp.do_gunzip

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Default: ``false``.

	Set to ``true`` to gunzip the object while storing it in the
	cache.

	If ``http_gzip_support`` is disabled, setting this variable
	has no effect.

	It is a VCL error to use beresp.do_gunzip after setting beresp.filters.


.. _beresp.do_gzip:

beresp.do_gzip

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Default: ``false``.

	Set to ``true`` to gzip the object while storing it.

	If ``http_gzip_support`` is disabled, setting this variable
	has no effect.

	It is a VCL error to use beresp.do_gzip after setting beresp.filters.


.. _beresp.do_stream:

beresp.do_stream

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Default: ``true``.

	Deliver the object to the client while fetching the whole
	object into varnish.

	For uncacheable objects, storage for parts of the body which
	have been sent to the client may get freed early, depending
	on the storage engine used.

	This variable has no effect if beresp.do_esi is true or when
	the response body is empty.


.. _beresp.filters:

beresp.filters

	Type: STRING

	Readable from: vcl_backend_response

	Writable from: vcl_backend_response

	List of Varnish Fetch Processor (VFP) filters the beresp.body
	will be pulled through. The order left to right signifies
	processing from backend to cache, iow the leftmost filter is
	run first on the body as received from the backend after
	decoding of any transfer encodings.

	VFP Filters change the body before going into the cache and/or
	being handed to the client side, where it may get processed
	again by resp.filters.

	The following VFP filters exist in varnish-cache:

	* ``gzip``: compress a body using gzip

	* ``testgunzip``: Test if a body is valid gzip and refuse it
	  otherwise

	* ``gunzip``: Uncompress gzip content

	* ``esi``: ESI-process plain text content

	* ``esi_gzip``: Save gzipped snippets for efficient
	  ESI-processing

	  This filter enables stitching together ESI from individually
	  gzipped fragments, saving processing power for
	  re-compression on the client side at the expense of some
	  compression efficiency.

	Additional VFP filters are available from VMODs.

	By default, beresp.filters is constructed as follows:

	* ``gunzip`` gets added for gzipped content if
	  ``beresp.do_gunzip`` or ``beresp.do_esi`` are true.

	* ``esi_gzip`` gets added if ``beresp.do_esi`` is true
	  together with ``beresp.do_gzip`` or content is already
	  compressed.

	* ``esi`` gets added if ``beresp.do_esi`` is true

	* ``gzip`` gets added for uncompressed content if
	  ``beresp.do_gzip`` is true

	* ``testgunzip`` gets added for compressed content if
	  ``beresp.do_gunzip`` is false.

	After beresp.filters is set, using any of the beforementioned
	``beresp.do_*`` switches is a VCL error.


.. _beresp.grace:

beresp.grace

	Type: DURATION

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Default: Cache-Control ``stale-while-revalidate`` directive,
	or ``default_grace`` parameter.

	Set to a period to enable grace.


.. _beresp.http:

beresp.http.*

	Type: HEADER

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Unsettable from: vcl_backend_response, vcl_backend_error

	The HTTP headers returned from the server.

	See req.http_ for general notes.


beresp.http.content-length

	Type: HEADER

	Readable from: vcl_backend_response, vcl_backend_error

	The content-length header field is protected, see protected_headers_.


beresp.http.transfer-encoding

	Type: HEADER

	Readable from: vcl_backend_response, vcl_backend_error

	The transfer-encoding header field is protected, see protected_headers_.


.. _beresp.keep:

beresp.keep

	Type: DURATION

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Default: ``default_keep`` parameter.

	Set to a period to enable conditional backend requests.

	The keep time is cache lifetime in addition to the ttl.

	Objects with ttl expired but with keep time left may be used
	to issue conditional (If-Modified-Since / If-None-Match)
	requests to the backend to refresh them.


beresp.proto	``VCL <= 4.0``

	Type: STRING

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	The HTTP protocol version the backend replied with.


.. _beresp.proto:

beresp.proto	``VCL >= 4.1``

	Type: STRING

	Readable from: vcl_backend_response, vcl_backend_error

	The HTTP protocol version the backend replied with.


.. _beresp.reason:

beresp.reason

	Type: STRING

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	The HTTP status message returned by the server.


.. _beresp.status:

beresp.status

	Type: INT

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	The HTTP status code returned by the server.

	More information in the `HTTP response status`_ section.


.. _beresp.storage:

beresp.storage

	Type: STEVEDORE

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error


	The storage backend to use to save this object.

beresp.storage_hint	``VCL <= 4.0``

	Type: STRING

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error


	Deprecated since varnish 5.1 and discontinued since VCL
	4.1 (varnish 6.0). Use beresp.storage instead.

	Hint to Varnish that you want to save this object to a
	particular storage backend.


.. _beresp.time:

beresp.time

	Type: TIME

	Readable from: vcl_backend_response, vcl_backend_error

	When the backend headers were fully received just before
	``vcl_backend_response {}`` was entered, or when
	``vcl_backend_error {}`` was entered.

.. _beresp.transit_buffer:

beresp.transit_buffer

	Type: BYTES

	Readable from: vcl_backend_response

	Writable from: vcl_backend_response

	Default: ``transit_buffer`` parameter, see :ref:`varnishd(1)`.

	The maximum number of bytes the client can be ahead of the
	backend during a streaming pass if ``beresp`` is
	uncacheable. See also ``transit_buffer`` parameter
	documentation in :ref:`varnishd(1)`.


.. _beresp.ttl:

beresp.ttl

	Type: DURATION

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Default: Cache-Control ``s-maxage`` or ``max-age`` directives,
	or a value computed from the Expires header's deadline, or the
	``default_ttl`` parameter.

	The object's remaining time to live, in seconds.


.. _beresp.uncacheable:

beresp.uncacheable

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Inherited from bereq.uncacheable, see there.

	Setting this variable makes the object uncacheable.

	This may produce a hit-for-miss object in the cache.

	Clearing the variable has no effect and will log the warning
	"Ignoring attempt to reset beresp.uncacheable".


.. _beresp.was_304:

beresp.was_304

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error


	When ``true`` this indicates that we got a 304 response
	to our conditional fetch from the backend and turned
	that into ``beresp.status = 200``


obj
---

This is the object we found in cache.  It cannot be modified.

.. _obj.age:

obj.age

	Type: DURATION

	Readable from: vcl_hit, vcl_deliver

	The age of the object.


.. _obj.can_esi:

obj.can_esi

	Type: BOOL

	Readable from: vcl_hit, vcl_deliver

	If the object can be ESI processed, that is if setting
	``resp.do_esi`` or adding ``esi`` to ``resp.filters`` in
	``vcl_deliver {}`` would cause the response body to be ESI
	processed.


.. _obj.grace:

obj.grace

	Type: DURATION

	Readable from: vcl_hit, vcl_deliver

	The object's grace period in seconds.


.. _obj.hits:

obj.hits

	Type: INT

	Readable from: vcl_hit, vcl_deliver


	The count of cache-hits on this object.

	In `vcl_deliver` a value of 0 indicates a cache miss.


.. _obj.http:

obj.http.*

	Type: HEADER

	Readable from: vcl_hit

	The HTTP headers stored in the object.

	See req.http_ for general notes.


.. _obj.keep:

obj.keep

	Type: DURATION

	Readable from: vcl_hit, vcl_deliver

	The object's keep period in seconds.


.. _obj.proto:

obj.proto

	Type: STRING

	Readable from: vcl_hit

	The HTTP protocol version stored in the object.


.. _obj.reason:

obj.reason

	Type: STRING

	Readable from: vcl_hit


	The HTTP reason phrase stored in the object.


.. _obj.status:

obj.status

	Type: INT

	Readable from: vcl_hit


	The HTTP status code stored in the object.

	More information in the `HTTP response status`_ section.


.. _obj.storage:

obj.storage

	Type: STEVEDORE

	Readable from: vcl_hit, vcl_deliver

	The storage backend where this object is stored.


.. _obj.time:

obj.time

	Type: TIME

	Readable from: vcl_hit, vcl_deliver

	The time the object was created from the perspective of the
	server which generated it. This will roughly be equivalent to
	``now`` - ``obj.age``.


.. _obj.ttl:

obj.ttl

	Type: DURATION

	Readable from: vcl_hit, vcl_deliver

	The object's remaining time to live, in seconds.


.. _obj.uncacheable:

obj.uncacheable

	Type: BOOL

	Readable from: vcl_deliver

	Whether the object is uncacheable (pass, hit-for-pass or
	hit-for-miss).


resp
----

This is the response we send to the client, it is built from either
``beresp`` (pass/miss), ``obj`` (hits) or created from whole cloth (synth).

With the exception of ``resp.body`` all ``resp.*`` variables available
in both ``vcl_deliver{}`` and ``vcl_synth{}`` as a matter of symmetry.

resp

	Type: HTTP

	Readable from: vcl_deliver, vcl_synth

	The entire response HTTP data structure, useful as argument
	to VMODs.


.. _resp.body:

resp.body

	Type: BODY

	Writable from: vcl_synth

	To produce a synthetic response body, for instance for errors.


.. _resp.do_esi:

resp.do_esi	``VCL >= 4.1``

	Type: BOOL

	Readable from: vcl_deliver, vcl_synth

	Writable from: vcl_deliver, vcl_synth

	Default: obj.can_esi

	This can be used to selectively disable ESI processing, even
	though ESI parsing happened during fetch (see beresp.do_esi).
	This is useful when Varnish caches peer with each other.

	It is a VCL error to use resp.do_esi after setting resp.filters.


.. _resp.filters:

resp.filters

	Type: STRING

	Readable from: vcl_deliver, vcl_synth

	Writable from: vcl_deliver, vcl_synth

	List of VDP filters the resp.body will be pushed through.

	Before resp.filters is set, the value read will be the default
	filter list as determined by varnish based on resp.do_esi and
	request headers.

	After resp.filters is set, changing any of the conditions
	which otherwise determine the filter selection will have no
	effect. Using resp.do_esi is an error once resp.filters is
	set.


.. _resp.http:

resp.http.*

	Type: HEADER

	Readable from: vcl_deliver, vcl_synth

	Writable from: vcl_deliver, vcl_synth

	Unsettable from: vcl_deliver, vcl_synth

	The HTTP headers that will be returned.

	See req.http_ for general notes.


resp.http.content-length

	Type: HEADER

	Readable from: vcl_deliver, vcl_synth

	The content-length header field is protected, see protected_headers_.


resp.http.transfer-encoding

	Type: HEADER

	Readable from: vcl_deliver, vcl_synth

	The transfer-encoding header field is protected, see protected_headers_.


.. _resp.is_streaming:

resp.is_streaming

	Type: BOOL

	Readable from: vcl_deliver, vcl_synth

	Returns true when the response will be streamed
	while being fetched from the backend.


resp.proto	``VCL <= 4.0``

	Type: STRING

	Readable from: vcl_deliver, vcl_synth

	Writable from: vcl_deliver, vcl_synth

	The HTTP protocol version to use for the response.


.. _resp.proto:

resp.proto	``VCL >= 4.1``

	Type: STRING

	Readable from: vcl_deliver, vcl_synth

	The HTTP protocol version to use for the response.


.. _resp.reason:

resp.reason

	Type: STRING

	Readable from: vcl_deliver, vcl_synth

	Writable from: vcl_deliver, vcl_synth

	The HTTP status message that will be returned.


.. _resp.status:

resp.status

	Type: INT

	Readable from: vcl_deliver, vcl_synth

	Writable from: vcl_deliver, vcl_synth

	The HTTP status code that will be returned.

	More information in the `HTTP response status`_ section.

	resp.status 200 will get changed into 304 by core code after
	a return(deliver) from vcl_deliver for conditional requests
	to cached content if validation succeeds.

	For the validation, first ``req.http.If-None-Match`` is
	compared against ``resp.http.Etag``. If they compare equal
	according to the rules for weak validation (see RFC7232), a
	304 is sent.

	Secondly, ``req.http.If-Modified-Since`` is compared against
	``resp.http.Last-Modified`` or, if it is unset or weak, against
        the point in time when the object was last modified based on the
	``Date`` and ``Age`` headers received with the backend
	response which created the object. If the object has not been
	modified based on that comparison, a 304 is sent.


.. _resp.time:

resp.time

	Type: TIME

	Readable from: vcl_deliver, vcl_synth

	The time when we started preparing the response, just before
	entering ``vcl_synth {}`` or ``vcl_deliver {}``.


Special variables
-----------------

.. _now:

now

	Type: TIME

	Readable from: all


	The current time, in seconds since the UNIX epoch.

	When converted to STRING in expressions it returns
	a formatted timestamp like ``Tue, 20 Feb 2018 09:30:31 GMT``

	``now`` remains stable for the duration of any built-in VCL
	subroutine to make time-based calculations predictable and
	avoid edge cases.

	In other words, even if considerable amounts of time are spent
	in VCL, ``now`` will always represent the point in time when
	the respective built-in VCL subroutine was entered. ``now`` is
	thus not suitable for any kind of time measurements. See
	:ref:`std.timestamp()`, :ref:`std.now()` and
	:ref:`std.timed_call()` in :ref:`vmod_std(3)`.

sess
----

A session corresponds to the "conversation" that Varnish has with a
single client connection, over which one or more request/response
transactions may take place. It may comprise the traffic over an
HTTP/1 keep-alive connection, or the multiplexed traffic over an
HTTP/2 connection.

.. _sess.idle_send_timeout:

sess.idle_send_timeout

	Type: DURATION

	Readable from: client

	Writable from: client

	Unsettable from: client

	Send timeout for individual pieces of data on client
	connections, defaults to the ``idle_send_timeout`` parameter,
	see :ref:`varnishd(1)`


.. _sess.send_timeout:

sess.send_timeout

	Type: DURATION

	Readable from: client

	Writable from: client

	Unsettable from: client

	Total timeout for ordinary HTTP1 responses, defaults to the
	``send_timeout`` parameter, see :ref:`varnishd(1)`


.. _sess.timeout_idle:

sess.timeout_idle

	Type: DURATION

	Readable from: client

	Writable from: client

	Unsettable from: client

	Idle timeout for this session, defaults to the
	``timeout_idle`` parameter, see :ref:`varnishd(1)`


.. _sess.timeout_linger:

sess.timeout_linger

	Type: DURATION

	Readable from: client

	Writable from: client

	Unsettable from: client

	Linger timeout for this session, defaults to the
	``timeout_linger`` parameter, see :ref:`varnishd(1)`


.. _sess.xid:

sess.xid	``VCL >= 4.1``

	Type: INT

	Readable from: client, backend

	Unique ID of this session.


storage
-------

.. XXX all of these are actually defined in generate.py

.. _storage.free_space:

storage.<name>.free_space

	Type: BYTES

	Readable from: client, backend

	Default: 0

	Free space available in the named stevedore. Only available for
	the malloc stevedore.


.. _storage.happy:

storage.<name>.happy

	Type: BOOL

	Readable from: client, backend

	Default: true

	Health status for the named stevedore. Not available in any of the
	current stevedores.


.. _storage.used_space:

storage.<name>.used_space

	Type: BYTES

	Readable from: client, backend

	Default: 0

	Used space in the named stevedore. Only available for the malloc
	stevedore.

