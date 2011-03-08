.. _tutorial-handling_misbehaving_servers:

Misbehaving servers
-------------------

A key feature of Varnish is its ability to shield you from misbehaving
web- and application servers.



Grace mode
~~~~~~~~~~

When several clients are requesting the same page Varnish will send
one request to the backend and place the others on hold while fetching
one copy from the back end. In some products this is called request
coalescing and Varnish does this automatically.

If you are serving thousands of hits per second the queue of waiting
requests can get huge. There are to potential problems - one is a
thundering heard problem - suddenly releasing a thousand threads to
serve content might send to load sky high. Secondly - nobody likes to
wait. To deal with this Varnish can we can instruct Varnish to keep
the objects in cache beyond their TTL and to serve the waiting
requests somewhat stale content.

So, in order to serve stale content we must first have some content to
serve. So to make Varnish keep all objects for 30 minutes beyond their
TTL use the following VCL:::

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
:ref:`tutorial-advanced_backend_servers-health` you can check if the
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

Saint mode
~~~~~~~~~~

Sometimes servers get flaky. They start throwing out random
errors. You can instruct Varnish to try to handle this in a
more-than-graceful way - enter *Saint mode*. Saint mode enables you to
discard a certain page from one backend server and either try another
server or serve stale content from cache. Lets have a look at how this
can be enabled in VCL:::

  sub vcl_fetch {
    if (beresp.status == 500) { 
      set beresp.saintmode = 10s;
      restart;
    }
    set beresp.grace = 5m;
  } 

When we set beresp.saintmode to 10 seconds Varnish will not ask *that*
server for URL for 10 seconds. A blacklist, more or less. Also a
restart is performed so if you have other backends capable of serving
that content Varnish will try those. When you are out of backends
Varnish will serve the content from its stale cache.

This can really be a life saver.

Known limitations on grace- and saint mode
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If your request fails while it is being fetched you're thrown into
vcl_error. vcl_error has access to a rather limited set of data so you
can't enable saint mode or grace mode here. This will be addressed in a
future release but a work-around is available.

* Declare a backend that is always sick.
* Set a magic marker in vcl_error
* Restart the transaction
* Note the magic marker in vcl_recv and set the backend to the one mentioned
* Varnish will now serve stale data is any is available


God mode
~~~~~~~~
Not implemented yet. :-)

