

Altering the backend response
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Here we override the TTL of a object coming from the backend if it
matches certain criteria::

  sub vcl_backend_response {
     if (bereq.url ~ "\.(png|gif|jpg)$") {
       unset beresp.http.set-cookie;
       set beresp.ttl = 1h;
    }
  }



We also remove any Set-Cookie headers in order to avoid a `hit-for-pass`
object to be created. See :ref:`user-guide-vcl_actions`.
