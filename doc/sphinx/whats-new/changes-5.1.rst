.. _whatsnew_changes_5.1:

Changes in Varnish 5.1
======================

Varnish 5.1 ... XXX


Progress on HTTP/2 support
~~~~~~~~~~~~~~~~~~~~~~~~~~

XXX

.. _whatsnew_changes_5.1_hitpass:

Hit-For-Pass has returned
~~~~~~~~~~~~~~~~~~~~~~~~~

As hinted in :ref:`whatsnew_changes_5.0`, we have restored the
possibility of invoking the old hit-for-pass feature in VCL. The
treatment of uncacheable content that was new in version 5.0, which we
have taken to calling "hit-for-miss", remains the default. Now you can
choose hit-for-pass with ``return(pass(DURATION))`` from
``vcl_backend_response``, setting the duration of the hit-for-pass
state in the argument to ``pass``. For example:
``return(pass(120s))``.

To recap: when ``beresp.uncacheable`` is set to ``true`` in
``vcl_backend_response``, Varnish makes a note of it with a mininal
object in the cache, and finds that information again on the next
lookup for the same object. In essence, the cache is used to remember
that the last backend response was not cacheable. In that case,
Varnish proceeds as with a cache miss, so that the response may become
cacheable on subsequent requests. The difference is that Varnish does
not perform request coalescing, as it does for ordinary misses, when a
response has been marked uncacheable. For ordinary misses, when there
are requests pending for the same object at the same time, only one
fetch is executed at a time, since the response may be cached, in
which case the cached response may be used for the remaining
requests. But this is not done for "hit-for-miss" objects, since they
are known to have been uncacheable on the previous fetch.

``builtin.vcl`` sets ``beresp.uncacheable`` to ``true`` when a number
of conditions hold for a backend response that indicate that it should
not be cached, for example if the TTL has been determined to be 0
(perhaps due to a ``Cache-Control`` header), or if a ``Set-Cookie``
header is present in the response. So hit-for-miss is the default
for uncacheable backend responses.

A consequence of this is that fetches for uncacheable responses cannot
be conditional in the default case. That is, the backend request may
not include the headers ``If-Modified-Since`` or ``If-None-Match``,
which might cause the backend to return status "304 Not Modified" with
no response body. Since the response to a cache miss might be cached,
there has to be a body to cache, and this is true of hit-for-miss as
well. If either of those two headers were present in the client
request, they are removed from the backend request for a miss or
hit-for-miss.

Since conditional backend requests and the 304 response may be
critical to performance for non-cacheable content, especially if the
response body is large, we have made the old hit-for-pass feature
available again, with ``return(pass(DURATION))`` in VCL.

As with hit-for-miss, Varnish uses the cache to make a note of
hit-for-pass objects, and finds them again on subsequent lookups.  The
requests are then processed as for ordinary passes (``return(pass)``
from ``vcl_recv``) -- there is no request coalescing, and the response
will not be cached, even if it might have been otherwise.
``If-Modified-Since`` or ``If-None-Match`` headers in the client
request are passed along in the backend request, and a backend
response with status 304 and no body is passed back to the client.

The hit-for-pass state of an object lasts for the time given as the
DURATION in the previous return from ``vcl_backend_response``.  After
the "hit-for-pass TTL" elapses, the next request will be an ordinary
miss. So a hit-for-pass object cannot become cacheable again until
that much time has passed.

304 Not Modified responses after a pass
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Related to the previous topic, there has been a change in the way
Varnish handles a very specific case: deciding whether to send a "304
Not Modified" response to the client after a pass, when the backend
had the opportunity to send a 304 response, but chose not to by
sending a 200 response status instead.

Previously, Varnish went along with the backend when this happened,
sending the 200 response together with the response body to the
client. This was the case even if the backend set the response headers
``ETag`` and/or ``Last-Modified`` so that, when compared to the
request headers ``If-None-Match`` and ``If-Modified-Since``, a 304
response would seem to be warranted. Since those headers are passed
back to the client, the result could appear a bit odd from the
client's perspective -- the client used the request headers to ask if
the response was unmodified, and the response headers seem to indicate
that it wasn't, and yet the response status suggests that it was.

Now the decision to send a 304 client response status is made solely
at delivery time, based on the contents of the client request headers
and the headers in the response that Varnish is preparing to send,
regardless of whether the backend fetch was a pass. So Varnish may
send a 304 client response after a pass, even though the backend chose
not to, having seen the same request headers (if the response headers
permit it).

We made this change for consistency -- for hits, misses, hit-for-miss,
hit-for-pass, and now pass, the decision to send a 304 client response
is based solely on the contents of client request headers and the
response headers.

You can restore the previous behavior -- don't send a 304 client
response on pass if the backend didn't -- with VCL means, either by
removing the ``ETag`` or ``Last-Modified`` headers in
``vcl_backend_response``, or by removing the If-* client request
headers in ``vcl_pass``.


News for authors of VMODs and Varnish API client applications
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* The VRT version has been bumped to 6.0, since there have been some
  changes and additions to the ABI. See ``vrt.h`` for an overview.

* In particular, there have been some changes to the ``WS_*``
  interface for accessing workspaces. We are working towards fully
  encapsulating workspaces with the ``WS_*`` family of functions, so
  that it should not be necessary to access the internals of a
  ``struct ws``, which may be revised in a future release. There are
  no revisions at present, so your code won't break if you're
  working with the innards of a ``struct ws`` now, but you would be
  prudent to replace that code with ``WS_*`` calls some time before
  the next release. And please let us know if there's something you
  need to do that the workspace interface won't handle.

* ``libvarnishapi.so`` now exports more symbols from Varnish internal
  libraries:

  * All of the ``VTIM_*`` functions -- getting clock times, formatting
    and parsing date & time formats, sleeping and so forth.

  * All of the ``VSB_*`` functions for working with safe string
    buffers.
