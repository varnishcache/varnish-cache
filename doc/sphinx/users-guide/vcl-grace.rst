.. _users-guide-handling_misbehaving_servers:

Misbehaving servers
-------------------

A key feature of Varnish is its ability to shield you from misbehaving
web- and application servers.


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
wait. To deal with this we can instruct Varnish to keep
the objects in cache beyond their TTL and to serve the waiting
requests somewhat stale content.

So, in order to serve stale content we must first have some content to
serve. So to make Varnish keep all objects for 30 minutes beyond their
TTL use the following VCL::

  sub vcl_fetch {
    set beresp.grace = 30m;
  }

Varnish still won't serve the stale objects. In order to enable
Varnish to actually serve the stale object we must enable this on the
request. Lets us say that we accept serving 15s old object.::

  sub vcl_recv {
    set req.grace = 15s;
  }

You might wonder why we should keep the objects in the cache for 30
minutes if we are unable to serve them? Well, if you have enabled
:ref:`users-guide-advanced_backend_servers-health` you can check if the
backend is sick and if it is we can serve the stale content for a bit
longer.::

   if (! req.backend.healthy) {
      set req.grace = 5m;
   } else {
      set req.grace = 15s;
   }

So, to sum up, grace mode solves two problems:
 * it serves stale content to avoid request pile-up.
 * it serves stale content if the backend is not healthy.

