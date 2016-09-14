.. _whatsnew_changes_5.0:

Changes in Varnish 5.0
======================

Varnish 5.0 changes some (mostly) internal APIs and adds some major new
features over Varnish 4.1.


Separate VCL files and VCL labels
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Varnish 5.0 supports jumping from the active VCL's vcl_recv{} to
another VCL via a VCL label.

The major use of this will probably be to have a separate VCL for
each domain/vhost, in order to untangle complex VCL files, but
it is not limited to this criteria, it would also be possible to
send all POSTs, all JPEG images or all traffic from a certain
IP range to a separate VCL file.

VCL labels can also be used to give symbolic names to loaded VCL
configurations, so that operations personnel only need to know
about "normal", "weekend" and "emergency", and web developers
can update these as usual, without having to tell ops what the
new weekend VCL is called.


Very Experimental HTTP/2 support
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

We are in the process of adding HTTP/2 support to Varnish, but
the code is very green still - life happened.

But you can actually get a bit of traffic though it already, and
we hope to have it production ready for the next major release
(2017-03-15).

Varnish supports HTTP/1 -> 2 upgrade.  For political reasons,
no browsers support that, but tools like curl does.

For encrypted HTTP/2 traffic, put a SSL proxy in front of Varnish.


The Shard Director
~~~~~~~~~~~~~~~~~~

We have added to the directors vmod an overhauled version of a
director which was available as an out-of-tree vmod under the name
VSLP for a couple of years: It's basically a better hash director,
which uses consistent hashing to provide improved stability of backend
node selection when the configuration and/or health state of backends
changes. There are several options to provide the shard key. The
rampup feature allows to take just-gone-healthy backends in production
smoothly, while the prewarm feature allows to prepare backends for
traffic which they would see if the primary backend for a certain key
went down.

It can be reconfigured dynamically (outside vcl_init), but different
to our other directors, configuration is transactional: Any series of
backend changes must be concluded by a reconfigure call for
activation.

Hit-For-Pass is now actually Hit-For-Miss
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Almost since the beginning of time (2008), varnish has hit-for-pass:
It is basically a negative caching feature, putting into the cache
objects as markers saying "when you hit this, your request should be a
pass". The purpose is to selectively avoid the request coalescing
(waitinglist) feature, which is useful for cacheable content, but not
for uncacheable objects. If we did not have hit-for-pass, without
additional configuration in vcl_recv, requests to uncacheable content
would be sent to the backend serialized (one after the other).

As useful as this feature is, it has caused a lot of headaches to
varnish administrators along the lines of "why the *beep* doesn't
Varnish cache this": A hit-for-pass object stayed in cache for however
long its ttl dictated and prevented caching whenever it got hit ("for
that url" in most cases). In particular, as a pass object can not be
turned into something cacheable retrospectively (beresp.uncacheable
can be changed from false to true, but not the other way around), even
responses which would have been cacheable were not cached. So, when a
hit-for-pass object got into cache unintentionally, it had to be
removed explicitly (using a ban or purge).

We've changed this now:

A hit-for-pass object (we still call it like this in the docs, logging
and statistics) will now cause a cache-miss for all subsequent
requests, so if any backend response qualifies for caching, it will
get cached and subsequent requests will be hits.

The punchline is: We've changed from "the uncacheable case wins" to
"the cacheable case wins" or from hit-for-pass to hit-for-miss.

The primary consequence which we are aware of at the time of this
release is caused be the fact that, to create cacheable objects, we
need to make backend requests unconditional (that is, remove the
If-Modified-Since and If-None-Match headers): For conditional client
requests on hit-for-pass objects, Varnish will now issue an
unconditional backend fetch and, for 200 responses, send a 304 or 200
response to the client as appropriate.

As of the time of this release we cannot say if this will remain the
final word on this topic, but we hope that it will mean an improvement
for most users of Varnish.
