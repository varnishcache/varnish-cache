
remote
~~~~~~

remote.ip

	Type: IP

	Readable from: client, backend

	
	The IP address of the other end of the TCP connection.
	This can either be the clients IP, or the outgoing IP
	of a proxy server.
	

client
~~~~~~

client.ip

	Type: IP

	Readable from: client, backend

	
	The client's IP address.
	

client.identity

	Type: STRING

	Readable from: client

	Writable from: client

	
	Identification of the client, used to load balance
	in the client director. Defaults to the client's IP
	address.
	

local
~~~~~

local.endpoint

	Type: STRING

	Readable from: client

	
	The transport address of the '-a' socket the session was
	accepted on.
	

local.socket

	Type: STRING

	Readable from: client

	
	The name of the '-a' socket the session was accepted on.
	

local.ip

	Type: IP

	Readable from: client, backend

	
	The IP address of the local end of the TCP connection.
	

server
~~~~~~

server.ip

	Type: IP

	Readable from: client, backend

	
	The IP address of the socket on which the client
	connection was received.
	

server.hostname

	Type: STRING

	Readable from: all

	
	The host name of the server.
	

server.identity

	Type: STRING

	Readable from: all

	
	The identity of the server, as set by the -i
	parameter.  If the -i parameter is not passed to varnishd,
	server.identity will be set to the hostname of the machine.
	

req
~~~

req

	Type: HTTP

	Readable from: client

	
	The entire request HTTP data structure
	

req.method

	Type: STRING

	Readable from: client

	Writable from: client

	
	The request type (e.g. "GET", "HEAD").
	

req.hash

	Type: BLOB

	Readable from: vcl_hit, vcl_miss, vcl_pass, vcl_purge, vcl_deliver

	
	The hash key of this request.
	

req.url

	Type: STRING

	Readable from: client

	Writable from: client

	
	The requested URL.
	

req.proto

	Type: STRING

	Readable from: client

	Writable from: client

	
	The HTTP protocol version used by the client.
	

req.http.

	Type: HEADER

	Readable from: client

	Writable from: client

	Unsetable from: client

	
	The corresponding HTTP header.
	

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
	
	Usage of req.ttl should be replaced with a check on
	obj.ttl in vcl_hit, returning miss when needed, but
	this currently hits bug #1799, so an additional
	workaround is required.
	
	Deprecated and scheduled for removal with varnish release 7.
	

req.xid

	Type: STRING

	Readable from: client

	
	Unique ID of this request.
	

req.esi

	Type: BOOL

	Readable from: client

	Writable from: client

	
	Boolean. Set to false to disable ESI processing
	regardless of any value in beresp.do_esi. Defaults
	to true. This variable is subject to change in
	future versions, you should avoid using it.
	

req.can_gzip

	Type: BOOL

	Readable from: client

	
	Does the client accept the gzip transfer encoding.
	

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

	
	Ignore any busy object during cache lookup. You
	would want to do this if you have two server looking
	up content from each other to avoid potential deadlocks.
	

req.hash_always_miss

	Type: BOOL

	Readable from: client

	Writable from: client

	
	Force a cache miss for this request. If set to true
	Varnish will disregard any existing objects and
	always (re)fetch from the backend.
	

req_top
~~~~~~~

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
	

req_top.http.

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

bereq

	Type: HTTP

	Readable from: backend

	
	The entire backend request HTTP data structure
	

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

	
	The request body.
	

bereq.hash

	Type: BLOB

	Readable from: vcl_pipe, backend

	
	The hash key of this request.
	

bereq.method

	Type: STRING

	Readable from: vcl_pipe, backend

	Writable from: vcl_pipe, backend

	
	The request type (e.g. "GET", "HEAD").
	

bereq.url

	Type: STRING

	Readable from: vcl_pipe, backend

	Writable from: vcl_pipe, backend

	
	The requested URL.
	

bereq.proto

	Type: STRING

	Readable from: vcl_pipe, backend

	Writable from: vcl_pipe, backend

	
	The HTTP protocol version used to talk to the server.
	

bereq.http.

	Type: HEADER

	Readable from: vcl_pipe, backend

	Writable from: vcl_pipe, backend

	Unsetable from: vcl_pipe, backend

	
	The corresponding HTTP header.
	

bereq.uncacheable

	Type: BOOL

	Readable from: backend

	
	Indicates whether this request is uncacheable due
	to a pass in the client side or a hit on an hit-for-pass
	object.
	

bereq.connect_timeout

	Type: DURATION

	Readable from: vcl_pipe, backend

	Writable from: vcl_pipe, backend

	
	The time in seconds to wait for a backend connection.
	

bereq.first_byte_timeout

	Type: DURATION

	Readable from: backend

	Writable from: backend

	
	The time in seconds to wait for the first byte from
	the backend.  Not available in pipe mode.
	

bereq.between_bytes_timeout

	Type: DURATION

	Readable from: backend

	Writable from: backend

	
	The time in seconds to wait between each received byte from the
	backend.  Not available in pipe mode.
	

bereq.is_bgfetch

	Type: BOOL

	Readable from: backend

	
	True for background fetches.
	

beresp
~~~~~~

beresp

	Type: HTTP

	Readable from: vcl_backend_response, vcl_backend_error

	
	The entire backend response HTTP data structure
	

beresp.body

	Type: BODY

	Writable from: vcl_backend_error

	
	The response body.
	

beresp.proto

	Type: STRING

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	
	The HTTP protocol version used the backend replied with.
	

beresp.status

	Type: INT

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	
	The HTTP status code returned by the server.
	
	Status codes >1000 can be set for vcl-internal
	purposes and will be taken modulo 1000 on delivery.
	

beresp.reason

	Type: STRING

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	
	The HTTP status message returned by the server.
	

beresp.http.

	Type: HEADER

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	Unsetable from: vcl_backend_response, vcl_backend_error

	
	The corresponding HTTP header.
	

beresp.do_esi

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	
	Boolean. ESI-process the object after fetching it.
	Defaults to false. Set it to true to parse the
	object for ESI directives. Will only be honored if
	req.esi is true.
	

beresp.do_stream

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	
	Deliver the object to the client while fetching the whole
	object into varnish. For uncacheable objects, storage for
	parts of the body which have been sent to the client may
	get freed early, depending on the storage engine used.
	

beresp.do_gzip

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	
	Boolean. Gzip the object before storing it. Defaults
	to false. When http_gzip_support is on Varnish will
	request already compressed content from the backend
	and as such compression in Varnish is not needed.
	

beresp.do_gunzip

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	
	Boolean. Unzip the object before storing it in the
	cache.  Defaults to false.
	

beresp.was_304

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error

	
	Boolean. If this is a successful 304 response to a
	backend conditional request refreshing an existing
	cache object.
	

beresp.uncacheable

	Type: BOOL

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	
	Inherited from bereq.uncacheable, see there.
	
	Setting this variable makes the object uncacheable, which may
	get stored as a hit-for-miss object in the cache.
	
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
	

beresp.backend.ip

	Type: IP

	Readable from: vcl_backend_response

	
	IP of the backend this response was fetched from.
	

beresp.storage

	Type: STEVEDORE

	Readable from: vcl_backend_response, vcl_backend_error

	Writable from: vcl_backend_response, vcl_backend_error

	
	The storage backend to use to save this object.
	

obj
~~~

obj.proto

	Type: STRING

	Readable from: vcl_hit

	
	The HTTP protocol version stored with the object.
	

obj.status

	Type: INT

	Readable from: vcl_hit

	
	The HTTP status code stored with the object.
	

obj.reason

	Type: STRING

	Readable from: vcl_hit

	
	The HTTP reason phrase stored with the object.
	

obj.hits

	Type: INT

	Readable from: vcl_hit, vcl_deliver

	
	The count of cache-hits on this object. A value of 0 indicates a
	cache miss.
	

obj.http.

	Type: HEADER

	Readable from: vcl_hit

	
	The corresponding HTTP header.
	

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

	
	The object's remaining grace period in seconds.
	

obj.keep

	Type: DURATION

	Readable from: vcl_hit, vcl_deliver

	
	The object's remaining keep period in seconds.
	

obj.uncacheable

	Type: BOOL

	Readable from: vcl_deliver

	
	Whether the object is uncacheable (pass, hit-for-pass or
	hit-for-miss).
	

obj.storage

	Type: STEVEDORE

	Readable from: vcl_hit, vcl_deliver

	
	The storage backend used to save this object.
	

resp
~~~~

resp

	Type: HTTP

	Readable from: vcl_deliver, vcl_synth

	
	The entire response HTTP data structure.
	

resp.body

	Type: BODY

	Writable from: vcl_synth

	
	The response body.
	

resp.proto

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
	

resp.http.

	Type: HEADER

	Readable from: vcl_deliver, vcl_synth

	Writable from: vcl_deliver, vcl_synth

	Unsetable from: vcl_deliver, vcl_synth

	
	The corresponding HTTP header.
	

resp.is_streaming

	Type: BOOL

	Readable from: vcl_deliver, vcl_synth

	
	Returns true when the response will be streamed
	from the backend.
	

now
~~~

now

	Type: TIME

	Readable from: all

	
	The current time, in seconds since the epoch. When
	used in string context it returns a formatted string.
	

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
	
