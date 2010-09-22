.. _tutorial-handling_misbehaving_servers:

Misbehaving servers
-------------------

A key feature of Varnish is its ability to shield you from misbehaving
web- and application servers.



Grace mode
~~~~~~~~~~

When several clients are requesting the same page Varnish will send
one request to the backend and place the others on hold while fetching
one copy from the back end. 

If you are serving thousands of hits per second this queue can get
huge. Nobody likes to wait so there is a possibility to serve stale
content to waiting users. In order to do this we must instruct Varnish
to keep the objects in cache beyond their TTL. So, to keep all objects
for 30 minutes beyond their TTL use the following VCL:::

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
backend is healthy and serve the content for as longer.::

   if (! req.backend.healthy) {
      set req.grace = 5m;
   } else {
      set req.grace = 15s;
   }
  
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

God mode
~~~~~~~~
Not implemented yet. :-)

