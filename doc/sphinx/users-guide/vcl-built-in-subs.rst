
.. _vcl-built-in-subs:

.. XXX This document needs substational review.


Built in subroutines
--------------------


vcl_recv
~~~~~~~~

Called at the beginning of a request, after the complete request has
been received and parsed.  Its purpose is to decide whether or not to
serve the request, how to do it, and, if applicable, which backend to
use.

It is also used to modify the request, something you'll probably find
yourself doing frequently. 

The vcl_recv subroutine may terminate with calling ``return()`` on one
of the following keywords:

  error code [reason]
    Return the specified error code to the client and abandon the request.

  pass
    Switch to pass mode.  Control will eventually pass to vcl_pass.

  pipe
    Switch to pipe mode.  Control will eventually pass to vcl_pipe.

  lookup
    Look up the requested object in the cache.  Control will
    eventually pass to vcl_hit or vcl_miss, depending on whether the
    object is in the cache.  The ``bereq.method`` value will be set
    to ``GET`` regardless of the value of ``req.method``.



vcl_backend_fetch
~~~~~~~~~~~~~~~~~

Called before sending the backend request. In this subroutine you
typically alter the request before it gets to the backend.

.. XXX Return statements?


vcl_backend_response
~~~~~~~~~~~~~~~~~~~

Called after a document has been successfully retrieved from the backend.

The vcl_backend_response subroutine may terminate with calling return() with one
of the following keywords:

  deliver
    Possibly insert the object into the cache, then deliver it to the
    client.  Control will eventually pass to vcl_deliver.

  error code [reason]
    Return the specified error code to the client and abandon the request.

  hit_for_pass
    Pass in fetch. Passes the object without caching it. This will
    create a so-called hit_for_pass object which has the side effect
    that the decision not to cache will be cached. This is to allow
    would-be uncachable requests to be passed to the backend at the
    same time. The same logic is not necessary in vcl_recv because
    this happens before any potential queueing for an object takes
    place.  Note that the TTL for the hit_for_pass object will be set
    to what the current value of beresp.ttl is. Control will be
    handled to vcl_deliver on the current request, but subsequent
    requests will go directly to vcl_pass based on the hit_for_pass
    object.

  restart
    Restart the transaction. Increases the restart counter. If the number
    of restarts is higher than *max_restarts* Varnish emits a guru meditation
    error.

vcl_deliver
~~~~~~~~~~~

Called before a cached object is delivered to the client.

The vcl_deliver subroutine may terminate with one of the following
keywords:

  deliver
    Deliver the object to the client.

  restart
    Restart the transaction. Increases the restart counter. If the number
    of restarts is higher than *max_restarts* Varnish emits a guru meditation
    error.

vcl_backend_error
~~~~~~~~~~~~~~~~~

Called when we hit an error, either explicitly or implicitly due to
backend or internal errors.

The vcl_backend_error subroutine may terminate by calling return with one of
the following keywords:

  deliver
    Deliver the error object to the client.

  restart
    Restart the transaction. Increases the restart counter. If the number
    of restarts is higher than *max_restarts* Varnish emits a guru meditation
    error.


vcl_pipe
~~~~~~~~

Called upon entering pipe mode.  In this mode, the request is passed
on to the backend, and any further data from either client or
backend is passed on unaltered until either end closes the
connection.

 The vcl_pipe subroutine may terminate with calling return() with one of
 the following keywords:

  error code [reason]
    Return the specified error code to the client and abandon the request.

  pipe
    Proceed with pipe mode.

vcl_pass
~~~~~~~~

Called upon entering pass mode.  In this mode, the request is passed
on to the backend, and the backend's response is passed on to the
client, but is not entered into the cache.  Subsequent requests
submitted over the same client connection are handled normally.

The vcl_pass subroutine may terminate with calling return() with one
of the following keywords:

  error code [reason]
    Return the specified error code to the client and abandon the request.

  pass
    Proceed with pass mode.

  restart
    Restart the transaction. Increases the restart counter. If the number
    of restarts is higher than *max_restarts* Varnish emits a guru meditation
    error.

vcl_miss
~~~~~~~~

Called after a cache lookup if the requested document was not found in
the cache.  Its purpose is to decide whether or not to attempt to
retrieve the document from the backend, and which backend to use.

The vcl_miss subroutine may terminate with calling return() with one
of the following keywords:

  error code [reason]
    Return the specified error code to the client and abandon the request.

  pass
    Switch to pass mode.  Control will eventually pass to vcl_pass.

  fetch
    Retrieve the requested object from the backend.  Control will
    eventually pass to vcl_fetch.



vcl_init
~~~~~~~~

Called when VCL is loaded, before any requests pass through it.
Typically used to initialize VMODs.

  return() values:

  ok
    Normal return, VCL continues loading.


vcl_fini
~~~~~~~~

Called when VCL is discarded only after all requests have exited the VCL.
Typically used to clean up VMODs.

  return() values:

  ok
    Normal return, VCL will be discarded.
