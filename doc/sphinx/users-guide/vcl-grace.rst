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
serve. So to make Varnish keep all objects for 2 minutes beyond their
TTL use the following VCL::

  sub vcl_backend_response {
    set beresp.grace = 2m;
  }

Now Varnish will be allowed to serve objects that are up to two
minutes out of date. When it does it will also schedule a refresh of
the object. This will happen asynchronously and the moment the new
object is in it will replace the one we've already got.

You can influence how this logic works by adding code in vcl_hit. The
default looks like this:::

  sub vcl_hit {
     if (obj.ttl >= 0s) {
         // A pure unadultered hit, deliver it
         return (deliver);
     }
     if (obj.ttl + obj.grace > 0s) {
         // Object is in grace, deliver it
         // Automatically triggers a background fetch
         return (deliver);
     }
     // fetch & deliver once we get the result
     return (fetch);
  }

The grace logic is pretty obvious here. If you have enabled
:ref:`users-guide-advanced_backend_servers-health` you can check if
the backend is sick and only serve graced object then. Replace the
second if-clause with something like this:::

   if (!std.healthy(req.backend_hint) && (obj.ttl + obj.grace > 0s)) {
         return (deliver);
   } else {
         return (fetch);
   }

So, to sum up, grace mode solves two problems:
 * it serves stale content to avoid request pile-up.
 * it serves stale content if you allow it.

