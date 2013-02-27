

Altering the backend response
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Here we override the TTL of a object comming from the backend if it
matches certain criteria::

  sub vcl_fetch {
     if (req.url ~ "\.(png|gif|jpg)$") {
       unset beresp.http.set-cookie;
       set beresp.ttl = 1h;
    }
  }

.. XXX ref hit-for-pass

We also remove any Set-Cookie headers in order to avoid a hit-for-pass
object to be created.
