.. _vcl-built-in-subs:

====================
Built in subroutines
====================

Various built-in subroutines are called during processing of client-
and backend requests as well as upon ``vcl.load`` and ``vcl.discard``.

-----------
client side
-----------

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

  ``hash``
    Continue processing the object as a potential candidate for
    caching. Passes the control over to :ref:`vcl_hash`.

  ``pass``
    Switch to pass mode. Control will eventually pass to :ref:`vcl_pass`.

  ``pipe``
    Switch to pipe mode. Control will eventually pass to :ref:`vcl_pipe`.

  ``synth(status code, reason)``
    Transition to :ref:`vcl_synth` with ``resp.status`` and
    ``resp.reason`` being preset to the arguments of ``synth()``.

  ``purge``
    Purge the object and it's variants. Control passes through
    vcl_hash to vcl_purge.

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

  ``pipe``
    Proceed with pipe mode.

  ``synth(status code, reason)``
    Transition to :ref:`vcl_synth` with ``resp.status`` and
    ``resp.reason`` being preset to the arguments of ``synth()``.

.. _vcl_pass:

vcl_pass
~~~~~~~~

Called upon entering pass mode. In this mode, the request is passed
on to the backend, and the backend's response is passed on to the
client, but is not entered into the cache. Subsequent requests
submitted over the same client connection are handled normally.

The `vcl_pass` subroutine may terminate with calling ``return()`` with one
of the following keywords:

  ``fetch``
    Proceed with pass mode - initiate a backend request.

  ``restart``
    Restart the transaction. Increases the restart counter. If the number
    of restarts is higher than *max_restarts* Varnish emits a guru meditation
    error.

  ``synth(status code, reason)``
    Transition to :ref:`vcl_synth` with ``resp.status`` and
    ``resp.reason`` being preset to the arguments of ``synth()``.

.. _vcl_hit:

vcl_hit
~~~~~~~

Called when a cache lookup is successful. The object being hit may be
stale: It can have a zero or negative `ttl` with only `grace` or
`keep` time left.

The `vcl_hit` subroutine may terminate with calling ``return()``
with one of the following keywords:

  ``deliver``
    Deliver the object. If it is stale, a background fetch to refresh
    it is triggered.

  ``fetch``
    Synchronously refresh the object from the backend despite the
    cache hit. Control will eventually pass to :ref:`vcl_miss`.

  ``pass``
    Switch to pass mode. Control will eventually pass to :ref:`vcl_pass`.

  ``restart``
    Restart the transaction. Increases the restart counter. If the number
    of restarts is higher than *max_restarts* Varnish emits a guru meditation
    error.

  ``synth(status code, reason)``
    Transition to :ref:`vcl_synth` with ``resp.status`` and
    ``resp.reason`` being preset to the arguments of ``synth()``.

.. XXX: #1603 hit should not go to miss

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

  ``fetch``
    Retrieve the requested object from the backend. Control will
    eventually pass to `vcl_backend_fetch`.

  ``pass``
    Switch to pass mode. Control will eventually pass to :ref:`vcl_pass`.

  ``restart``
    Restart the transaction. Increases the restart counter. If the number
    of restarts is higher than *max_restarts* Varnish emits a guru meditation
    error.

  ``synth(status code, reason)``
    Transition to :ref:`vcl_synth` with ``resp.status`` and
    ``resp.reason`` being preset to the arguments of ``synth()``.

.. XXX: #1603 hit should not go to miss

.. _vcl_hash:

vcl_hash
~~~~~~~~

Called after `vcl_recv` to create a hash value for the request. This is
used as a key to look up the object in Varnish.

The `vcl_hash` subroutine may only terminate with calling ``return(lookup)``:

  ``lookup``
    Look up the object in cache.
    Control passes to :ref:`vcl_purge` when coming from a ``purge``
    return in `vcl_recv`.
    Otherwise control passes to :ref:`vcl_hit`, :ref:`vcl_miss` or
    :ref:`vcl_pass` if the cache lookup result was a hit, a miss or hit
    on a hit-for-pass object (object with ``obj.uncacheable ==
    true``), respectively.

.. _vcl_purge:

vcl_purge
~~~~~~~~~

Called after the purge has been executed and all its variants have been evited.

The `vcl_purge` subroutine may terminate with calling ``return()`` with one
of the following keywords:

  ``restart``
    Restart the transaction. Increases the restart counter. If the number
    of restarts is higher than *max_restarts* Varnish emits a guru meditation
    error.

  ``synth(status code, reason)``
    Transition to :ref:`vcl_synth` with ``resp.status`` and
    ``resp.reason`` being preset to the arguments of ``synth()``.

.. _vcl_deliver:

vcl_deliver
~~~~~~~~~~~

Called before any object except a `vcl_synth` result is delivered to the client.

The `vcl_deliver` subroutine may terminate with calling ``return()`` with one
of the following keywords:

  ``deliver``
    Deliver the object to the client.

  ``restart``
    Restart the transaction. Increases the restart counter. If the number
    of restarts is higher than *max_restarts* Varnish emits a guru meditation
    error.

  ``synth(status code, reason)``
    Transition to :ref:`vcl_synth` with ``resp.status`` and
    ``resp.reason`` being preset to the arguments of ``synth()``.

.. _vcl_synth:

vcl_synth
~~~~~~~~~

Called to deliver a synthetic object. A synthetic object is generated
in VCL, not fetched from the backend. Its body may be contructed using
the ``synthetic()`` function.

A `vcl_synth` defined object never enters the cache, contrary to a
:ref:`vcl_backend_error` defined object, which may end up in cache.

The subroutine may terminate with calling ``return()`` with one of the
following keywords:

  ``deliver``
    Directly deliver the object defined by `vcl_synth` to the
    client without calling `vcl_deliver`.

  ``restart``
    Restart the transaction. Increases the restart counter. If the number
    of restarts is higher than *max_restarts* Varnish emits a guru meditation
    error.

------------
Backend Side
------------

.. _vcl_backend_fetch:

vcl_backend_fetch
~~~~~~~~~~~~~~~~~

Called before sending the backend request. In this subroutine you
typically alter the request before it gets to the backend.

The `vcl_backend_fetch` subroutine may terminate with calling
``return()`` with one of the following keywords:

  ``fetch``
    Fetch the object from the backend.

  ``abandon``
    Abandon the backend request. Unless the backend request was a
    background fetch, control is passed to :ref:`vcl_synth` on the
    client side with ``resp.status`` preset to 503.

.. _vcl_backend_response:

vcl_backend_response
~~~~~~~~~~~~~~~~~~~~

Called after the response headers have been successfully retrieved from
the backend.

The `vcl_backend_response` subroutine may terminate with calling
``return()`` with one of the following keywords:

  ``deliver``
    For a 304 response, create an updated cache object.
    Otherwise, fetch the object body from the backend and initiate
    delivery to any waiting client requests, possibly in parallel
    (streaming).

  ``abandon``
    Abandon the backend request. Unless the backend request was a
    background fetch, control is passed to :ref:`vcl_synth` on the
    client side with ``resp.status`` preset to 503.

  ``retry``
    Retry the backend transaction. Increases the `retries` counter.
    If the number of retries is higher than *max_retries*,
    control will be passed to :ref:`vcl_backend_error`.

.. _vcl_backend_error:

vcl_backend_error
~~~~~~~~~~~~~~~~~

This subroutine is called if we fail the backend fetch or if
*max_retries* has been exceeded.

A synthetic object is generated in VCL, whose body may be contructed
using the ``synthetic()`` function.

The `vcl_backend_error` subroutine may terminate with calling ``return()``
with one of the following keywords:

  ``deliver``
    Deliver and possibly cache the object defined in
    `vcl_backend_error` **as if it was fetched from the backend**, also
    referred to as a "backend synth".

  ``retry``
    Retry the backend transaction. Increases the `retries` counter.
    If the number of retries is higher than *max_retries*,
    :ref:`vcl_synth` on the client side is called with ``resp.status``
    preset to 503.

----------------------
vcl.load / vcl.discard
----------------------

.. _vcl_init:

vcl_init
~~~~~~~~

Called when VCL is loaded, before any requests pass through it.
Typically used to initialize VMODs.

The `vcl_init` subroutine may terminate with calling ``return()``
with one of the following keywords:

  ``ok``
    Normal return, VCL continues loading.

  ``fail``
    Abort loading of this VCL.

.. _vcl_fini:

vcl_fini
~~~~~~~~

Called when VCL is discarded only after all requests have exited the VCL.
Typically used to clean up VMODs.

The `vcl_fini` subroutine may terminate with calling ``return()``
with one of the following keywords:

  ``ok``
    Normal return, VCL will be discarded.
