.. _vcl_variables:

VCL Variables
-------------

Variables provide read, write and delete access to almost all aspects
of the work at hand.

Reading a variable is done simply by using its name in VCL::

    if (client.ip ~ bad_guys) {
	return (synth(400));
    }

Writing a variable, where this is possible, is done with a `set`
statement::

    set resp.http.never = "Let You Down";

Similarly, deleting a variable, for the few variables where this is
possible, is done with a `unset` statement::

    unset req.http.cookie;

Which operations are possible on each variable is described below,
often with the shorthand "backend" which covers the `vcl_backend_*`
methods and "client" which covers the rest, except `vcl_init` and
`vcl_fini`.

When setting a variable, the right hand side of the equal sign
must have the variables type, you cannot assign a STRING to
a variable of type NUMBER, even if the string is `"42"`.
(Explicit conversion functions can be found in
:ref:`vmod_std(3)`).

local, server, remote and client
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

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


local.ip

	Type: IP

	Readable from: client, backend

	The IP address (and port number) of the local end of the
	TCP connection, for instance `192.168.1.1:81`

	If the connection is a UNIX domain socket, the value
	will be `0.0.0.0:0`

local.endpoint	``VCL >= 4.1``

	Type: STRING

	Readable from: client, backend

	The address of the '-a' socket the session was accepted on.

	If the argument was `-a foo=:81` this would be ":81"


local.socket	``VCL >= 4.1``

	Type: STRING

	Readable from: client, backend

	The name of the '-a' socket the session was accepted on.

	If the argument was `-a foo=:81` this would be "foo".

	Note that all '-a' gets a default name on the form `a%d`
	if no name is provided.

remote.ip

	Type: IP

	Readable from: client, backend

	The IP address of the other end of the TCP connection.
	This can either be the clients IP, or the outgoing IP
	of a proxy server.

	If the connection is a UNIX domain socket, the value
	will be `0.0.0.0:0`

client.ip

	Type: IP

	Readable from: client, backend


	The client's IP address, either the same as `local.ip`
	or what the PROXY protocol told us.

client.identity

	Type: STRING

	Readable from: client

	Writable from: client


	Identification of the client, used to load balance
	in the client director.  Defaults to `client.ip`

	This variable can be overwritten with more precise
	information, for instance extracted from a `Cookie:`
	header.


server.ip

	Type: IP

	Readable from: client, backend


	The IP address of the socket on which the client
	connection was received, either the same as `server.ip`
	or what the PROXY protocol told us.


server.hostname

	Type: STRING

	Readable from: all

	The host name of the server, as returned by the
	`gethostname(3)` system function.


server.identity

	Type: STRING

	Readable from: all

	The identity of the server, as set by the `-i` parameter.

	If an `-i` parameter is not passed to varnishd, the return
	value from `gethostname(3)` system function will be used.

req and req_top
~~~~~~~~~~~~~~~

These variables describe the present request, and when ESI:include
requests are being processed, req_top points to the request received
from the client.

req

	Type: HTTP

	Readable from: client


	The entire request HTTP data structure.
	Mostly useful for passing to VMODs.


req.method

	Type: STRING

	Readable from: client

	Writable from: client


	The request method (e.g. "GET", "HEAD", ...)


req.hash

	Type: BLOB

	Readable from: vcl_hit, vcl_miss, vcl_pass, vcl_purge, vcl_deliver


	The hash key of this request.
	Mostly useful for passing to VMODs, but can also be useful
	for debugging hit/miss status.


req.url

	Type: STRING

	Readable from: client

	Writable from: client


	The requested URL, for instance "/robots.txt".


req.proto	``VCL <= 4.0``

	Type: STRING

	Readable from: client

	Writable from: client

	The HTTP protocol version used by the client, usually "HTTP/1.1"
	or "HTTP/2.0".

req.proto	``VCL >= 4.1``

	Type: STRING

	Readable from: client

	The HTTP protocol version used by the client, usually "HTTP/1.1"
	or "HTTP/2.0".


req.http.*

	Type: HEADER

	Readable from: client

	Writable from: client

	Unsetable from: client


	The headers of request, things like `req.http.date`.

	The RFCs allow multiple headers with the same name, and both
	`set` and `unset` will remove *all* headers with the name given.


req.restarts

	Type: INT

	Readable from: client


	A count of how many times this request has been restarted.


req.storage

	Type: STEVEDORE

	Readable from: client

	Writable from: client


	The storage backend to use to save this request body.


req.esi_level

	Type: INT

	Readable from: client

	A count of how many levels of ESI requests we're currently at.

req.ttl

	Type: DURATION

	Readable from: client

	Writable from: client


	Upper limit on the object age for cache lookups to return hit.


req.grace

	Type: DURATION

	Readable from: client

	Writable from: client


	Upper limit on the object grace.

	During lookup the minimum of req.grace and the object's stored
	grace value will be used as the object's grace.


req.xid

	Type: STRING

	Readable from: client

	Unique ID of this request.

req.esi	``VCL <= 4.0``

	Type: BOOL

	Readable from: client

	Writable from: client

	Set to `false` to disable ESI processing
	regardless of any value in beresp.do_esi. Defaults
	to `true`. This variable is replaced by `resp.do_esi`
	in VCL 4.1.

req.can_gzip

	Type: BOOL

	Readable from: client

	True if the client provided `gzip` or `x-gzip` in the
	`Accept-Encoding` header.


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


req.hash_ignore_busy

	Type: BOOL

	Readable from: client

	Writable from: client

	Default: `false`

	Ignore any busy object during cache lookup.

	You only want to do this when you have two server looking
	up content sideways from each other to avoid deadlocks.


req.hash_always_miss

	Type: BOOL

	Readable from: client

	Writable from: client

	Default: `false`

	Force a cache miss for this request, even if perfectly
	good matching objects are in the cache.

	This is useful to force-update the cache without invalidating
	existing entries in case the fetch fails.


req_top.method

	Type: STRING

	Readable from: client

	The request method of the top-level request in a tree
	of ESI requests. (e.g. "GET", "HEAD").
	Identical to req.method in non-ESI requests.


req_top.url

	Type: STRING

	Readable from: client

	The requested URL of the top-level request in a tree
	of ESI requests.
	Identical to req.url in non-ESI requests.


req_top.http.*

	Type: HEADER

	Readable from: client

	HTTP headers of the top-level request in a tree of ESI requests.
	Identical to req.http. in non-ESI requests.


req_top.proto

	Type: STRING

	Readable from: client

	HTTP protocol version of the top-level request in a tree of
	ESI requests.
	Identical to req.proto in non-ESI requests.


bereq
~~~~~

This is the request we send to the backend, it is built from the
clients `req.*` fields by filtering out "per-hop" fields which
should not be passed along (`Connection:`, `Range:` and similar).

Slightly more fields are allowed through for `pass` fetches
than for `miss` fetches, for instance `Range`.

bereq

	Type: HTTP

	Readable from: backend

	The entire backend request HTTP data structure.
	Mostly useful as argument to VMODs.


bereq.xid

	Type: STRING

	Readable from: backend

	Unique ID of this request.


bereq.retries

	Type: INT

	Readable from: backend

	A count of how many times this request has been retried.


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


bereq.body

	Type: BODY

	Unsetable from: vcl_backend_fetch

	The request body, only present on `pass` requests.

	Unset will also remove `bereq.http.Content-Length`.

bereq.hash

	Type: BLOB

	Readable from: vcl_pipe, backend

	The hash key of this request, a copy of `req.hash`.


bereq.method

	Type: STRING

	Readable from: vcl_pipe, backend

	Writable from: vcl_pipe, backend

	The request type (e.g. "GET", "HEAD").

	Regular (non-pipe, non-pass) fetches are always "GET"


bereq.url

	Type: STRING

	Readable from: vcl_pipe, backend

	Writable from: vcl_pipe, backend

	The requested URL, copied from `req.url`


bereq.proto	``VCL <= 4.0``

	Type: STRING

	Readable from: vcl_pipe, backend

	Writable from: vcl_pipe, backend

	The HTTP protocol version, "HTTP/1.1" unless a pass or pipe
	request has "HTTP/1.0" in `req.proto`

bereq.proto	``VCL >= 4.1``

	Type: STRING

	Readable from: vcl_pipe, backend

	The HTTP protocol version, "HTTP/1.1" unless a pass or pipe
	request has "HTTP/1.0" in `req.proto`


bereq.http.*

	Type: HEADER

	Readable from: vcl_pipe, backend

	Writable from: vcl_pipe, backend

	Unsetable from: vcl_pipe, backend

	The headers to be sent to the backend.


bereq.uncacheable

	Type: BOOL

	Readable from: backend


	Indicates whether this request is uncacheable due to a
	`pass` in the client side or a hit on an hit-for-pass object.


bereq.connect_timeout

	Type: DURATION

	Readable from: vcl_pipe, backend

	Writable from: vcl_pipe, backend

	The time in seconds to wait for a backend connection to be
	established.


bereq.first_byte_timeout

	Type: DURATION

	Readable from: backend

	Writable from: backend

	The time in seconds to wait getting the first byte back
	from the backend.  Not available in pipe mode.


bereq.between_bytes_timeout

	Type: DURATION

	Readable from: backend

	Writable from: backend

	The time in seconds to wait between each received byte from the
	backend.  Not available in pipe mode.


bereq.is_bgfetch

	Type: BOOL

	Readable from: backend

	True for fetches where the client got a hit on an object in
	grace, and this fetch was kicked of in the background to get
	a fresh copy.

beresp
~~~~~~

The response received from the backend, one cache misses, the
store object is built from `beresp`.

beresp

	Type: HTTP

	Readable from: vcl_backend_response, vcl_backend_error

	The entire backend response HTTP data structure, useful as
	argument to VMOD functions.

beresp.body

	Type: BODY

	Writable from: vcl_backend_error

	For producing a synthetic body.

beresp.proto	``VCL <= 4.0``

	Type: STRING

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	The HTTP protocol version the backend replied with.

beresp.proto	``VCL >= 4.1``

	Type: STRING

	Readable from: vcl_backend_response, vcl_backend_error

	The HTTP protocol version the backend replied with.


beresp.status

	Type: INT

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	The HTTP status code returned by the server.

	Status codes on the form XXYZZ can be set where
	XXYZZ is less than 65536 and Y is [1...9].
	Only YZZ will be sent back to clients.

	XX can be therefore be used to pass information
	around inside VCL, for instance `return(synth(22404))`
	from `vcl_recv{}` to `vcl_synth{}`

beresp.reason

	Type: STRING

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	The HTTP status message returned by the server.

beresp.http.*

	Type: HEADER

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Unsetable from: vcl_backend_response, vcl_backend_error

	The HTTP headers returned from the server.

beresp.do_esi

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Default: false

	Set it to true to parse the object for ESI directives.
	Will only be honored if req.esi is true.


beresp.do_stream

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Default: true

	Deliver the object to the client while fetching the whole
	object into varnish.

	For uncacheable objects, storage for parts of the body which
	have been sent to the client may get freed early, depending
	on the storage engine used.

	This variable has no effect if do_esi is true or when the
	response body is empty.

beresp.do_gzip

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Default: false

	Set to `true` to gzip the object while storing it.

	If `http_gzip_support` is disabled, setting this variable
	has no effect.

beresp.do_gunzip

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Default: false

	Set to `true` to gunzip the object while storing it in the
	cache.

	If `http_gzip_support` is disabled, setting this variable
	has no effect.

beresp.was_304

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error


	When `true` this indicates that we got a 304 response
	to our conditional fetch from the backend and turned
	that into `beresp.status = 200`

beresp.uncacheable

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Inherited from bereq.uncacheable, see there.

	Setting this variable makes the object uncacheable.

	This may may produce a hit-for-miss object in the cache.

	Clearing the variable has no effect and will log the warning
	"Ignoring attempt to reset beresp.uncacheable".


beresp.ttl

	Type: DURATION

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	The object's remaining time to live, in seconds.


beresp.age

	Type: DURATION

	Readable from: vcl_backend_response, vcl_backend_error

	The age of the object.


beresp.grace

	Type: DURATION

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Set to a period to enable grace.


beresp.keep

	Type: DURATION

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Set to a period to enable conditional backend requests.

	The keep time is cache lifetime in addition to the ttl.

	Objects with ttl expired but with keep time left may be used
	to issue conditional (If-Modified-Since / If-None-Match)
	requests to the backend to refresh them.


beresp.backend

	Type: BACKEND

	Readable from: vcl_backend_response, vcl_backend_error

	This is the backend we fetched from.  If bereq.backend
	was set to a director, this will be the backend selected
	by the director.
	When used in string context, returns its name.


beresp.backend.name

	Type: STRING

	Readable from: vcl_backend_response, vcl_backend_error

	Name of the backend this response was fetched from.
	Same as beresp.backend.


beresp.backend.ip	``VCL <= 4.0``

	Type: IP

	Readable from: vcl_backend_response

	IP of the backend this response was fetched from.

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

beresp.filters

	Type: STRING

	Readable from: vcl_backend_response

	Writable from: vcl_backend_response

	List of VFP filters the beresp.body will be pulled through.

obj
~~~

This is the object we found in cache.  It cannot be modified.

obj.proto

	Type: STRING

	Readable from: vcl_hit

	The HTTP protocol version stored in the object.


obj.status

	Type: INT

	Readable from: vcl_hit


	The HTTP status code stored in the object.


obj.reason

	Type: STRING

	Readable from: vcl_hit


	The HTTP reason phrase stored in the object.


obj.hits

	Type: INT

	Readable from: vcl_hit, vcl_deliver


	The count of cache-hits on this object.

	In `vcl_deliver` a value of 0 indicates a cache miss.


obj.http.*

	Type: HEADER

	Readable from: vcl_hit

	The HTTP headers stored in the object.


obj.ttl

	Type: DURATION

	Readable from: vcl_hit, vcl_deliver

	The object's remaining time to live, in seconds.


obj.age

	Type: DURATION

	Readable from: vcl_hit, vcl_deliver

	The age of the object.


obj.grace

	Type: DURATION

	Readable from: vcl_hit, vcl_deliver

	The object's grace period in seconds.


obj.keep

	Type: DURATION

	Readable from: vcl_hit, vcl_deliver

	The object's keep period in seconds.


obj.uncacheable

	Type: BOOL

	Readable from: vcl_deliver

	Whether the object is uncacheable (pass, hit-for-pass or
	hit-for-miss).


obj.storage

	Type: STEVEDORE

	Readable from: vcl_hit, vcl_deliver

	The storage backend where this object is stored.


resp
~~~~

This is the response we send to the client, it is built from either
`beresp` (pass/miss), `obj` (hits) or created from whole cloth (synth).

With the exception of `resp.body` all `resp.*` variables available
in both `vcl_deliver{}` and `vcl_synth{}` as a matter of symmetry.

resp

	Type: HTTP

	Readable from: vcl_deliver, vcl_synth

	The entire response HTTP data structure, useful as argument
	to VMODs.

resp.body

	Type: BODY

	Writable from: vcl_synth

	To produce a synthetic response body, for instance for errors.

resp.proto	``VCL <= 4.0``

	Type: STRING

	Readable from: vcl_deliver, vcl_synth

	Writable from: vcl_deliver, vcl_synth

	The HTTP protocol version to use for the response.

resp.proto	``VCL >= 4.1``

	Type: STRING

	Readable from: vcl_deliver, vcl_synth

	Writable from: vcl_deliver, vcl_synth

	The HTTP protocol version to use for the response.

resp.status

	Type: INT

	Readable from: vcl_deliver, vcl_synth

	Writable from: vcl_deliver, vcl_synth

	The HTTP status code that will be returned.

	Assigning a HTTP standardized code to resp.status will also
	set resp.reason to the corresponding status message.

	resp.status 200 will get changed into 304 by core code after
	a return(deliver) from vcl_deliver for conditional requests
	to cached content if validation succeeds.


resp.reason

	Type: STRING

	Readable from: vcl_deliver, vcl_synth

	Writable from: vcl_deliver, vcl_synth

	The HTTP status message that will be returned.


resp.http.*

	Type: HEADER

	Readable from: vcl_deliver, vcl_synth

	Writable from: vcl_deliver, vcl_synth

	Unsetable from: vcl_deliver, vcl_synth


	The HTTP headers that will be returned.

resp.do_esi	``VCL >= 4.1``

	Type: BOOL

	Readable from: vcl_deliver, vcl_synth

	Writable from: vcl_deliver, vcl_synth

	Default: Set if ESI parsing has happened.

	This can be used to selectively disable ESI processing,
	even though ESI parsing happened during fetch.
	This is useful when Varnish caches peer with each other.


resp.is_streaming

	Type: BOOL

	Readable from: vcl_deliver, vcl_synth

	Returns true when the response will be streamed
	while being fetched from the backend.


Special variables
~~~~~~~~~~~~~~~~~

now

	Type: TIME

	Readable from: all


	The current time, in seconds since the UNIX epoch.

	When converted to STRING in expressions it returns
	a formatted timestamp like `Tue, 20 Feb 2018 09:30:31 GMT`

sess
~~~~

A session corresponds to the "conversation" that Varnish has with a
single client connection, over which one or more request/response
transactions may take place. It may comprise the traffic over an
HTTP/1 keep-alive connection, or the multiplexed traffic over an
HTTP/2 connection.

sess.xid	``VCL >= 4.1``

	Type: STRING

	Readable from: client, backend

	Unique ID of this session.

storage
~~~~~~~

storage.<name>.free_space

	Type: BYTES

	Readable from: client, backend


	Free space available in the named stevedore. Only available for
	the malloc stevedore.


storage.<name>.used_space

	Type: BYTES

	Readable from: client, backend


	Used space in the named stevedore. Only available for the malloc
	stevedore.


storage.<name>.happy

	Type: BOOL

	Readable from: client, backend


	Health status for the named stevedore. Not available in any of the
	current stevedores.

