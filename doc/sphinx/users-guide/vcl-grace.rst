.. _users-guide-handling_misbehaving_servers:

Grace mode and keep
-------------------

Sometimes you want Varnish to serve content that is somewhat stale
instead of waiting for a fresh object from the backend. For example,
if you run a news site, serving a main page that is a few seconds old
is not a problem if this gives your site faster load times.

In Varnish this is achieved by using `grace mode`. A related idea
is `keep`, which is also explained here.

Grace mode
~~~~~~~~~~

When several clients are requesting the same page Varnish will send
one request to the backend and place the others on hold while fetching
one copy from the backend. In some products this is called request
coalescing and Varnish does this automatically.

If you are serving thousands of hits per second the queue of waiting
requests can get huge. There are two potential problems - one is a
thundering herd problem - suddenly releasing a thousand threads to
serve content might send the load sky high. Secondly - nobody likes to
wait.

Setting an object's `grace` to a positive value tells Varnish that it
should serve the object to clients for some time after the TTL has
expired, while Varnish fetches a new version of the object. The default
value is controlled by the runtime parameter ``default_grace``.

Keep
~~~~

Setting an object's `keep` tells Varnish that it should keep an object
in the cache for some additional time. The reasons to set `keep` is to
use the object to construct a conditional GET backend request (with
If-Modified-Since: and/or Ìf-None-Match: headers), allowing the
backend to reply with a 304 Not Modified response, which may be more
efficient on the backend and saves re-transmitting the unchanged body.

The values are additive, so if grace is 10 seconds and keep is 1 minute,
then objects will survive in cache for 70 seconds after the TTL has
expired.

Setting grace and keep
~~~~~~~~~~~~~~~~~~~~~~

We can use VCL to make Varnish keep all objects for 10 minutes beyond
their TTL with a grace period of 2 minutes::

  sub vcl_backend_response {
       set beresp.grace = 2m;
       set beresp.keep = 8m;
  }

The effect of grace and keep
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For most users setting the default grace and/or a suitable grace for
each object is enough. The default VCL will do the right thing and
behave as described above. However, if you want to customize how
varnish behaves, then you should know some of the details on how this
works.

When ``sub vcl_recv`` ends with ``return (lookup)`` (which is the
default behavior), Varnish will look for a matching object in its
cache. Then, if it only found an object whose TTL has run out, Varnish
will consider the following:

* Is there already an ongoing backend request for the object?
* Is the object within the `grace period`?

Then, Varnish reacts using the following rules:

* If the `grace period` has run out and there is no ongoing backend
  request, then ``sub vcl_miss`` is called immediately, and the object
  will be used as a 304 candidate.
* If the `grace period` has run out and there is an ongoing backend
  request, then the request will wait until the backend request
  finishes.
* If there is no backend request for the object, one is scheduled.
* Assuming the object will be delivered, ``sub vcl_hit`` is called
  immediately.

Note that the backend fetch happens asynchronously, and the moment the
new object is in it will replace the one we've already got.

If you do not define your own ``sub vcl_hit``, then the default one is
used. It looks like this::

  sub vcl_hit {
       if (obj.ttl >= 0s) {
            // A pure unadulterated hit, deliver it
            return (deliver);
       }
       if (obj.ttl + obj.grace > 0s) {
            // Object is in grace, deliver it
            // Automatically triggers a background fetch
            return (deliver);
       }
       // fetch & deliver once we get the result
       return (miss);
  }

The effect of the built-in VCL is in fact equivalent to the following::

  sub vcl_hit {
       return (deliver);
  }

This is because ``obj.ttl + obj.grace > 0s`` always will evaluate to
true. However, the the VCL is as it is to show users how to
differentiate between a pure hit and a `grace` hit. With the next
major version of Varnish, the default VCL is planned to change to the
latter, shorter version.

Misbehaving servers
~~~~~~~~~~~~~~~~~~~

A key feature of Varnish is its ability to shield you from misbehaving
web- and application servers.

If you have enabled :ref:`users-guide-advanced_backend_servers-health`
you can check if the backend is sick and modify the behavior when it
comes to grace. This can done in the following way::

  sub vcl_backend_response {
       set beresp.grace = 24h;
       // no keep - the grace should be enough for 304 candidates
  }

  sub vcl_recv {
       if (std.healthy(req.backend_hint)) {
            // change the behavior for healthy backends: Cap grace to 10s
            set req.grace = 10s;
       }
  }

In the example above, the special variable ``req.grace`` is set.  The
effect is that, when the backend is healthy, objects with grace above
10 seconds will have an `effective` grace of 10 seconds.  When the
backend is sick, the default VCL kicks in, and the long grace is used.

Additionally, you might want to stop cache insertion when a backend fetch
returns an ``5xx`` error::

  sub vcl_backend_response {
       if (beresp.status >= 500 && bereq.is_bgfetch) {
            return (abandon);
       }
  }

Summary
~~~~~~~

Grace mode allows Varnish to deliver slightly stale content to clients
while getting a fresh version from the backend. The result is faster
load times at lower cost.

It is possible to limit the grace during lookup by setting
``req.grace`` and then change the behavior when it comes to
grace. Often this is done to change the `effective` grace depending on
the health of the backend.
