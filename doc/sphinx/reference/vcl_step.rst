
.. _vcl_steps:

VCL Steps
=========

Client side
###########

.. _vcl_recv:

vcl_recv
~~~~~~~~

Called at the beginning of a request, after the complete request has
been received and parsed, after a `restart` or as the result of an ESI
include.

Its purpose is to decide whether or not to serve the request, possibly
modify it and decide on how to process it further. A backend hint may
be set as a default for the backend processing side.

The `vcl_recv` subroutine may terminate with calling ``return()`` on one
of the following keywords:

  |
  | ``fail``
  |  see :ref:`fail` section above
  |
  | ``synth(status code, reason)``
  |  see :ref:`synth` section above
  |
  | ``restart``
  |  see :ref:`restart` section above
  |
  | ``pass``
  |  see :ref:`pass` section above
  |
  | ``pipe``
  |  see :ref:`pipe` section above
  |
  | ``hash``
  |  Continue processing the object as a potential candidate for
  |  caching. Passes the control over to :ref:`vcl_hash`.
  |
  | ``purge``
  |  Purge the object and it's variants. Control passes through
  |  :ref:`vcl_hash` to :ref:`vcl_purge`.
  |
  | ``vcl(label)``
  |  Switch to vcl labelled *label*.
  |
  |  This will roll back the request as if ``std.rollback(req)`` was
  |  called and continue vcl processing in :ref:`vcl_recv` of the vcl
  |  labelled *label* as if it was the active vcl.
  |
  |  The ``vcl(label)`` return is only valid while the ``req.restarts``
  |  count is zero and if used from the active vcl.
  |
  |  See the :ref:`ref_cli_vcl_label` command in :ref:`varnish-cli(7)`.

.. _vcl_pipe:

vcl_pipe
~~~~~~~~

Called upon entering pipe mode. In this mode, the request is passed on
to the backend, and any further data from both the client and backend
is passed on unaltered until either end closes the
connection. Basically, Varnish will degrade into a simple TCP proxy,
shuffling bytes back and forth. For a connection in pipe mode, no
other VCL subroutine will ever get called after `vcl_pipe`.

The `vcl_pipe` subroutine may terminate with calling ``return()`` with one
of the following keywords:

  |
  | ``fail``
  |  see :ref:`fail` section above
  |
  | ``synth(status code, reason)``
  |  see :ref:`synth` section above
  |
  | ``pipe``
  |  Proceed with pipe mode.

.. _vcl_pass:

vcl_pass
~~~~~~~~

Called upon entering pass mode. In this mode, the request is passed
on to the backend, and the backend's response is passed on to the
client, but is not entered into the cache. Subsequent requests
submitted over the same client connection are handled normally.

The `vcl_pass` subroutine may terminate with calling ``return()`` with one
of the following keywords:

  |
  | ``fail``
  |  see :ref:`fail` section above
  |
  | ``synth(status code, reason)``
  |  see :ref:`synth` section above
  |
  | ``restart``
  |  see :ref:`restart` section above
  |
  | ``fetch``
  |  Proceed with pass mode - initiate a backend request.

.. _vcl_hash:

vcl_hash
~~~~~~~~

Called after `vcl_recv` to create a hash value for the request. This is
used as a key to look up the object in Varnish.

The `vcl_hash` subroutine may terminate with calling ``return()`` with one
of the following keywords:

  |
  | ``fail``
  |  see  :ref:`fail` section above
  |
  | ``lookup``
  |  Look up the object in cache.
  |
  |  Control passes to :ref:`vcl_purge` when coming from a ``purge``
  |  return in `vcl_recv`.
  |
  |  Otherwise control passes to the next subroutine depending on the
  |  result of the cache lookup:
  |
  |  * a hit: pass to :ref:`vcl_hit`
  |
  |  * a miss or a hit on a hit-for-miss object (an object with
  |    ``obj.uncacheable == true``): pass to :ref:`vcl_miss`
  |
  |  * a hit on a hit-for-pass object (for which ``pass(DURATION)`` had been
  |    previously returned from ``vcl_backend_response``): pass to
  |    :ref:`vcl_pass`

.. _vcl_purge:

vcl_purge
~~~~~~~~~

Called after the purge has been executed and all its variants have been evicted.

The `vcl_purge` subroutine may terminate with calling ``return()`` with one
of the following keywords:

  |
  | ``fail``
  |  see :ref:`fail` section above
  |
  | ``synth(status code, reason)``
  |  see :ref:`synth` section above
  |
  | ``restart``
  |  see :ref:`restart` section above

.. _vcl_miss:

vcl_miss
~~~~~~~~

Called after a cache lookup if the requested document was not found in
the cache or if :ref:`vcl_hit` returned ``fetch``.

Its purpose is to decide whether or not to attempt to retrieve the
document from the backend. A backend hint may be set as a default for
the backend processing side.

The `vcl_miss` subroutine may terminate with calling ``return()`` with one
of the following keywords:

  |
  | ``fail``
  |  see :ref:`fail` section above
  |
  | ``synth(status code, reason)``
  |  see :ref:`synth` section above
  |
  | ``restart``
  |  see :ref:`restart` section above
  |
  | ``pass``
  |  see :ref:`pass` section above
  |
  | ``fetch``
  |  Retrieve the requested object from the backend. Control will
  |  eventually pass to `vcl_backend_fetch`.

.. _vcl_hit:

vcl_hit
~~~~~~~

Called when a cache lookup is successful. The object being hit may be
stale: It can have a zero or negative `ttl` with only `grace` or
`keep` time left.

The `vcl_hit` subroutine may terminate with calling ``return()``
with one of the following keywords:

  | ``fail``
  |  see :ref:`fail` section above
  |
  | ``synth(status code, reason)``
  |  see :ref:`synth` section above
  |
  | ``restart``
  |  see :ref:`restart` section above
  |
  | ``pass``
  |  see :ref:`pass` section above
  |
  | ``deliver``
  |  Deliver the object. If it is stale, a background fetch to refresh
  |  it is triggered.

.. _vcl_deliver:

vcl_deliver
~~~~~~~~~~~

Called before any object except a `vcl_synth` result is delivered to the client.

The `vcl_deliver` subroutine may terminate with calling ``return()`` with one
of the following keywords:

  |
  | ``fail``
  |  see :ref:`fail` section above
  |
  | ``synth(status code, reason)``
  |  see :ref:`synth` section above
  |
  | ``restart``
  |  see :ref:`restart` section above
  |
  | ``deliver``
  |  Deliver the object to the client.

.. _vcl_synth:

vcl_synth
~~~~~~~~~

Called to deliver a synthetic object. A synthetic object is generated
in VCL, not fetched from the backend. Its body may be constructed using
the ``synthetic()`` function.

A `vcl_synth` defined object never enters the cache, contrary to a
:ref:`vcl_backend_error` defined object, which may end up in cache.

The subroutine may terminate with calling ``return()`` with one of the
following keywords:

  |
  | ``fail``
  |  see :ref:`fail` section above
  |
  | ``restart``
  |  see :ref:`restart` section above
  |
  | ``deliver``
  |  Directly deliver the object defined by `vcl_synth` to the client
  |  without calling `vcl_deliver`.

Backend Side
############

.. _vcl_backend_fetch:

vcl_backend_fetch
~~~~~~~~~~~~~~~~~

Called before sending the backend request. In this subroutine you
typically alter the request before it gets to the backend.

The `vcl_backend_fetch` subroutine may terminate with calling
``return()`` with one of the following keywords:

  |
  | ``fail``
  |  see :ref:`fail` section above
  |
  | ``abandon``
  |  see :ref:`abandon` section above
  |
  | ``fetch``
  |  Fetch the object from the backend.
  |
  | ``error(status code, reason)``
  |  Transition to :ref:`vcl_backend_error` with ``beresp.status`` and
  |  ``beresp.reason`` being preset to the arguments of ``error()`` if
  |  arguments are provided.

Before calling `vcl_backend_fetch`, Varnish core prepares the `bereq`
backend request as follows:

* Unless the request is a `pass`,

  * set ``bereq.method`` to ``GET`` and ``bereq.proto`` to
    ``HTTP/1.1`` and

  * set ``bereq.http.Accept_Encoding`` to ``gzip`` if
    :ref:`ref_param_http_gzip_support` is enabled.

* If there is an existing cache object to be revalidated, set
  ``bereq.http.If-Modified-Since`` from its ``Last-Modified`` header
  and/or set ``bereq.http.If-None-Match`` from its ``Etag`` header

* Set ``bereq.http.X-Varnish`` to the current transaction id (`vxid`)

These changes can be undone or modified in `vcl_backend_fetch` before
the backend request is issued.

In particular, to cache non-GET requests, ``req.method`` needs to be
saved to a header or variable in :ref:`vcl_recv` and restored to
``bereq.method``. Notice that caching non-GET requests typically also
requires changing the cache key in :ref:`vcl_hash` e.g. by also
hashing the request method and/or request body.

HEAD request can be satisfied from cached GET responses.

.. _vcl_backend_refresh:

vcl_backend_refresh
~~~~~~~~~~~~~~~~~~~

This subroutine is called after a legitimate 304 response is received from
the backend so that you can chose how to handle the object revalidation, the default
is to apply the RFC logic which is to merge the new headers with the stale ones.

  |
  | ``merge``
  |  Merge the headers we got from the backend response with
  |  the ones we had in the cached object.
  |
  | ``obj_stale``
  |  Return the cached object without updating it with the new
  |  response headers.
  |
  | ``beresp``
  |  Return the beresp object as it is at the end of this subroutine
  |  without executing the RFC merge logic.
  |
  | ``fail``
  |  see :ref:`fail` section above
  |
  | ``abandon``
  |  see :ref:`abandon` section above
  |
  | ``retry``
  |  Retry the backend transaction. Increases the `retries` counter.
  |  If the number of retries is higher than *max_retries*,
  |  control will be passed to :ref:`vcl_backend_error`.
  |
  | ``error(status code, reason)``
  |  Transition to :ref:`vcl_backend_error` with ``beresp.status`` and
  |  ``beresp.reason`` being preset to the arguments of ``error()`` if
  |  arguments are provided.

.. _vcl_backend_response:

vcl_backend_response
~~~~~~~~~~~~~~~~~~~~

Called after the response headers have been successfully retrieved from
the backend.

The `vcl_backend_response` subroutine may terminate with calling
``return()`` with one of the following keywords:

  |
  | ``fail``
  |  see :ref:`fail` section above
  |
  | ``abandon``
  |  see :ref:`abandon` section above
  |
  | ``deliver``
  |  For a 304 response, create an updated cache object.
  |  Otherwise, fetch the object body from the backend and initiate
  |  delivery to any waiting client requests, possibly in parallel
  |  (streaming).
  |
  | ``retry``
  |  Retry the backend transaction. Increases the `retries` counter.
  |  If the number of retries is higher than *max_retries*,
  |  control will be passed to :ref:`vcl_backend_error`.
  |
  | ``pass(duration)``
  |  Mark the object as a hit-for-pass for the given duration. Subsequent
  |  lookups hitting this object will be turned into passed transactions,
  |  as if ``vcl_recv`` had returned ``pass``.
  |
  | ``error(status code, reason)``
  |  Transition to :ref:`vcl_backend_error` with ``beresp.status`` and
  |  ``beresp.reason`` being preset to the arguments of ``error()`` if
  |  arguments are provided.

.. _vcl_backend_error:

vcl_backend_error
~~~~~~~~~~~~~~~~~

This subroutine is called if we fail the backend fetch or if
*max_retries* has been exceeded.

Returning with :ref:`abandon` does not leave a cache object.

If returning with ``deliver`` and a ``beresp.ttl > 0s``, a synthetic
cache object is generated in VCL, whose body may be constructed using
the ``synthetic()`` function.

When there is a waiting list on the object, the default ``ttl`` will
be positive (currently one second), set before entering
``vcl_backend_error``. This is to avoid request serialization and
hammering on a potentially failing backend.

Since these synthetic objects are cached in these special
circumstances, be cautious with putting private information there. If
you really must, then you need to explicitly set ``beresp.ttl`` to
zero in ``vcl_backend_error``.

The `vcl_backend_error` subroutine may terminate with calling ``return()``
with one of the following keywords:

  |
  | ``fail``
  |  see :ref:`fail` section above
  |
  | ``abandon``
  |  see :ref:`abandon` section above
  |
  | ``deliver``
  |  Deliver and possibly cache the object defined in
  |  `vcl_backend_error` **as if it was fetched from the backend**, also
  |  referred to as a "backend synth".
  |
  | ``retry``
  |  Retry the backend transaction. Increases the `retries` counter.
  |  If the number of retries is higher than *max_retries*,
  |  :ref:`vcl_synth` on the client side is called with ``resp.status``
  |  preset to 503.

During vcl.load / vcl.discard
#############################

.. _vcl_init:

vcl_init
~~~~~~~~

Called when VCL is loaded, before any requests pass through it.
Typically used to initialize VMODs.

The `vcl_init` subroutine may terminate with calling ``return()``
with one of the following keywords:

  |
  | ``ok``
  |  Normal return, VCL continues loading.
  |
  | ``fail``
  |  Abort loading of this VCL.

.. _vcl_fini:

vcl_fini
~~~~~~~~

Called when VCL is discarded only after all requests have exited the VCL.
Typically used to clean up VMODs.

The `vcl_fini` subroutine may terminate with calling ``return()``
with one of the following keywords:

  |
  | ``ok``
  |  Normal return, VCL will be discarded.
