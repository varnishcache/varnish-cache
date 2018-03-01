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
in the cache for some additional time. There are two reasons to do this:

* To use the object as a candidate for ``304 NOT MODIFIED`` from the server.
* To be able to serve the object when grace has expired but we have a
  problem with getting a fresh object from the backend. This will require
  a change in ``sub vcl_hit``, as described below.

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
behave as described above. However, if you want to customize how varnish
behaves by changing ``sub vcl_hit``, then you should know some of the
details on how this works. 

When a request is made for a resource where an object is found, but TTL
has run out, Varnish considers the following:

* Is there already an ongoing backend request for the object?
* Is the object within the `grace period`?

Then, Varnish reacts using the following rules:

* If there is no backend request for the object, one is scheduled and
  ``sub vcl_hit`` is called immediately.
* If there is a backend request going on, but the object is under grace,
  ``sub vcl_hit`` is called immediately.
* If there is a backend request going on, but the grace has expired,
  processing is halted until the backend request has finished and a
  fresh object is available.

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

If you follow the code, you see that Varnish delivers graced objects
while fetching fresh copies, but if grace has expired the clients have to
wait until a new copy is available.

Misbehaving servers
~~~~~~~~~~~~~~~~~~~

A key feature of Varnish is its ability to shield you from misbehaving
web- and application servers.

If you have enabled :ref:`users-guide-advanced_backend_servers-health`
you can check if the backend is sick and modify the behavior when it
comes to grace. There are essentially two ways of doing this. You can
explicitly deliver kept object (that is not within grace) when you see
that the backend is sick, or you can explicitly `not` serve an expired
object when you know that the backend is healthy. The two methods have
slightly different characteristics, as we shall see.

In both cases we assume that you avoid inserting objects into the cache
when you get certain errors from the backend, for example by using the
following::

  sub vcl_backend_response {
       if (beresp.status == 503 && bereq.is_bgfetch) {
            return (abandon);
       }
  }

Method 1: When the backend is healthy, use a lower grace value
==============================================================

Imagine that you have set an object's grace to a high value that you
wish to use when the backend is sick, for example::

  sub vcl_backend_response {
       set beresp.grace = 24h;
       // no keep
  }

Then you can use the following code as your ``sub vcl_hit``::

   if (std.healthy(req.backend_hint)) {
        // change the behavior for health backends: Cap grace to 10s
	if (obj.ttl + obj.grace > 0s && obj.ttl + 10s > 0s) {
             return (deliver);
        } else {
             return (miss);
	}
   }

The effect of this is that, when the backend is healthy, objects with
grace above 10 seconds will have an `effective` grace of 10 seconds.
When the backend is sick, the default VCL kicks in, and the long grace
is used.

This method has one potentially serious problem when more than one
client asks for an object that has expired its TTL. If the second of
these requests arrives after the effective grace, but before the first
request has completed, then the second request will be turned into a
`pass`.

In practice this method works well in most cases, but if you
experience excessive `pass` behavior, this translates to a reduced the
hit rate and higher load on the backend. When this happens you will
see the error message `vcl_hit{} returns miss without busy object` in
the log.

Method 2: When the backend is sick, deliver kept objects
========================================================

With this method, we assume that we have used `sub backend_response`
to set `beresp.grace` to a value that is suitable for healthy backends,
and with a `beresp.keep` that corresponds to the time we want to serve
the object when the backend is sick. For example::

  sub vcl_backend_response {
       set beresp.grace = 10s;
       set beresp.keep = 24h;
  }

The appropriate code for ``vcl_hit`` then becomes::

   if (!std.healthy(req.backend_hint) && (obj.ttl + obj.grace + obj.keep > 0s)) {
        return (deliver);
   }

Typically you can omit the second part of the if test due to the
expiry thread deleting objects where `grace + keep` has expired. It is
possible that the `expiry thread` can be lagging slightly behind, but
for almost all practical purposes you are probably fine with the
following::

   if (!std.healthy(req.backend_hint)) {
        return (deliver);
   }

The problem with this solution concerns requests that are waiting for
a backend fetch to finish. If the backend fetch gets to ``return
(abandon)``, then all the requests that are waiting will get to ``sub
vcl_hit`` with an `error object` created by the error handling
code/VCL. In other words, you risk that some clients will get errors
instead of the more desirable stale objects.

Summary
~~~~~~~

Grace mode allows Varnish delivered slightly stale content to clients while
getting a fresh version from the backend. The result is faster load times
with a low cost.

It is possible to change the behavior when it comes to grace and keep, for
example by changing the `effective` grace depending on the health of the
backend, but you have to be careful.
