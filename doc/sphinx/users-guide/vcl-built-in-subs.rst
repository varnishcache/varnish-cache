
.. _vcl-built-in-subs:

.. XXX:This document needs substantional review.


Built in subroutines
--------------------


vcl_recv
~~~~~~~~

Called at the beginning of a request, after the complete request has
been received and parsed. Its purpose is to decide whether or not to
serve the request, how to do it, and, if applicable, which backend to
use.

It is also used to modify the request, something you'll probably find
yourself doing frequently. 

The `vcl_recv` subroutine may terminate with calling ``return()`` on one
of the following keywords:

  synth 
    Return a synthetic object with the specified error code to the
    client and abandon the request.

  pass
    Switch to pass mode. Control will eventually pass to vcl_pass.

  pipe
    Switch to pipe mode. Control will eventually pass to vcl_pipe.

  hash
    Continue processing the object as a potential candidate for
    caching. Passes the control over to vcl_hash.

  purge
    Purge the object and it's variants. Control passes through 
    vcl_hash to vcl_purge.

vcl_pipe
~~~~~~~~

Called upon entering pipe mode. In this mode, the request is passed
on to the backend, and any further data from either client or backend
is passed on unaltered until either end closes the
connection. Basically, Varnish will degrade into a simple TCP proxy,
shuffling bytes back and forth.

The `vcl_pipe` subroutine may terminate with calling ``return()`` with one
of the following keywords:

  synth(error code, reason)
    Return the specified error code to the client and abandon the request.

  pipe
    Proceed with pipe mode.

vcl_pass
~~~~~~~~

Called upon entering pass mode. In this mode, the request is passed
on to the backend, and the backend's response is passed on to the
client, but is not entered into the cache. Subsequent requests
submitted over the same client connection are handled normally.

The `vcl_pass` subroutine may terminate with calling ``return()`` with one
of the following keywords:

  synth(error code, reason)
    Return the specified error code to the client and abandon the request.

  pass
    Proceed with pass mode.

  restart
    Restart the transaction. Increases the restart counter. If the number
    of restarts is higher than *max_restarts* Varnish emits a guru meditation
    error.


vcl_hit
~~~~~~~

Called when a cache lookup is successful. 

.. XXX: missing the "The `vcl_hit` subroutine may terminate with calling ``return()`` with one of the following keywords:" thing. benc


  restart
    Restart the transaction. Increases the restart counter. If the number
    of restarts is higher than *max_restarts* Varnish emits a guru meditation
    error.

  deliver
    Deliver the object. Control passes to `vcl_deliver`.

  synth(error code, reason)
    Return the specified error code to the client and abandon the request.


vcl_miss
~~~~~~~~

Called after a cache lookup if the requested document was not found in
the cache. Its purpose is to decide whether or not to attempt to
retrieve the document from the backend, and which backend to use.

The `vcl_miss` subroutine may terminate with calling ``return()`` with one
of the following keywords:

  synth(error code, reason)
    Return the specified error code to the client and abandon the request.

  pass
    Switch to pass mode. Control will eventually pass to `vcl_pass`.

  fetch
    Retrieve the requested object from the backend. Control will
    eventually pass to `vcl_fetch`.

vcl_hash
~~~~~~~~

Called after `vcl_recv` to create a hash value for the request. This is
used as a key to look up the object in Varnish.

  lookup
    Look up the object in cache. Control passes to vcl_miss, vcl_hit
    or vcl_purge.




vcl_purge
~~~~~~~~~

Called after the purge has been executed and all its variants have been evited.

  synth
    Produce a response.


vcl_deliver
~~~~~~~~~~~

Called before a cached object is delivered to the client.

The ``vcl_deliver`` subroutine may terminate calling ``return()`` with one
of the following keywords:

.. XXX: Should perhaps be return as above? benc

  deliver
    Deliver the object to the client.

  restart
    Restart the transaction. Increases the restart counter. If the number
    of restarts is higher than *max_restarts* Varnish emits a guru meditation
    error.


vcl_backend_fetch
~~~~~~~~~~~~~~~~~

Called before sending the backend request. In this subroutine you
typically alter the request before it gets to the backend.

.. XXX: Missing terminate..keywords sentence? benc

  fetch
    Fetch the object from the backend.

  abandon
    Abandon the backend request and generates an error.


vcl_backend_response
~~~~~~~~~~~~~~~~~~~~

Called after a response has been successfully retrieved from the
backend. The response is available as `beresp`. 

.. XXX: beresp comes out of the blue here. maybe a short description? benc

Note that Varnish might
not be talking to an actual client, so operations that require a
client to be present are not allowed. Specifically there is no `req
object` and restarts are not allowed.

.. XXX: I do not follow sentence above. benc

The `vcl_backend_response` subroutine may terminate with calling ``return()`` with one
of the following keywords:

  deliver
    Possibly insert the object into the cache, then deliver it to the
    Control will eventually pass to `vcl_deliver`. Caching is dependant
    on 'beresp.cacheable'.

.. XXX:A parameter? that is set how? benc
    

  error(error code, reason)
    Return the specified error code to the client and abandon the request.

  retry
    Retry the backend transaction. Increases the `retries` counter. If the number
    of retries is higher than *max_retries* Varnish emits a guru meditation
    error.

vcl_backend_error
~~~~~~~~~~~~~~~~~

This subroutine is called if we fail the backend fetch. 

.. XXX:Missing the terminate return structure? benc

  deliver
    Deliver the error.

  retry
    Retry the backend transaction. Increases the `retries` counter. If the number
    of retries is higher than *max_retries* Varnish emits a guru meditation
    error.


vcl_backend_error
~~~~~~~~~~~~~~~~~

.. XXX: Same name as section above? benc

Called when we hit an error, either explicitly or implicitly due to
backend or internal errors.

The `vcl_backend_error` subroutine may terminate by calling ``return()`` with one of
the following keywords:

  deliver
    Deliver the error object to the client.

  retry
    Retry the backend transaction. Increases the retries counter. If the number
    of retries is higher than *max_retries* Varnish emits a guru meditation
    error.


vcl_init
~~~~~~~~

Called when VCL is loaded, before any requests pass through it.
Typically used to initialize VMODs.

.. XXX: Missing the terminate return structure? benc

  ``return()`` values:

  ok
    Normal return, VCL continues loading.


vcl_fini
~~~~~~~~

Called when VCL is discarded only after all requests have exited the VCL.
Typically used to clean up VMODs.


.. XXX: Missing the terminate return structure? benc

  ``return()`` values:

  ok
    Normal return, VCL will be discarded.


.. XXX: Maybe end here with the detailed flowchart from the book together with a reference to the book? benc
