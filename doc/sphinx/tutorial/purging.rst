.. _tutorial-purging:

=====================
 Purging and banning
=====================

One of the most effective way of increasing your hit ratio is to
increase the time-to-live (ttl) of your objects. But, as you're aware
of, in this twitterific day of age serving content that is outdated is
bad for business.

The solution is to notify Varnish when there is fresh content
available. This can be done through two mechanisms. HTTP purging and
bans. First, let me explain the HTTP purges. 


HTTP Purges
===========

A *purge* is what happens when you pick out an object from the cache
and discard it along with its variants. Usually a purge is invoked
through HTTP with the method PURGE.

An HTTP purge is similar to an HTTP GET request, except that the
*method* is PURGE. Actually you can call the method whatever you'd
like, but most people refer to this as purging. Squid supports the
same mechanism. In order to support purging in Varnish you need the
following VCL in place::

  acl purge {
	  "localhost";
	  "192.168.55.0/24";
  }
  
  sub vcl_recv {
      	  # allow PURGE from localhost and 192.168.55...

	  if (req.request == "PURGE") {
		  if (!client.ip ~ purge) {
			  error 405 "Not allowed.";
		  }
		  return (lookup);
	  }
  }
  
  sub vcl_hit {
	  if (req.request == "PURGE") {
	          purge;
		  error 200 "Purged.";
	  }
  }
  
  sub vcl_miss {
	  if (req.request == "PURGE") {
	          purge;
		  error 200 "Purged.";
	  }
  }

As you can see we have used to new VCL subroutines, vcl_hit and
vcl_miss. When we call lookup Varnish will try to lookup the object in
its cache. It will either hit an object or miss it and so the
corresponding subroutine is called. In vcl_hit the object that is
stored in cache is available and we can set the TTL.

So for example.com to invalidate their front page they would call out
to Varnish like this::

  PURGE / HTTP/1.0
  Host: example.com

And Varnish would then discard the front page. This will remove all
variants as defined by Vary.

Bans
====

There is another way to invalidate content. Bans. You can think of
bans as a sort of a filter. You *ban* certain content from being
served from your cache. You can ban content based on any metadata we
have.

Support for bans is built into Varnish and available in the CLI
interface. For VG to ban every png object belonging on example.com
they could issue::

  ban req.http.host == "example.com" && req.http.url ~ "\.png$"

Quite powerful, really.

Bans are checked when we hit an object in the cache, but before we
deliver it. *An object is only checked against newer bans*. 

Bans that only match against beresp.* are also processed by a
background worker threads called the *ban lurker*. The ban lurker will
walk the heap and try to match objects and will evict the matching
objects. How aggressive the ban lurker is can be controlled by the
parameter ban_lurker_sleep. 

Bans that are older then the oldest objects in the cache are discarded
without evaluation.  If you have a lot of objects with long TTL, that
are seldom accessed you might accumulate a lot of bans. This might
impact CPU usage and thereby performance.

You can also add bans to Varnish via HTTP. Doing so requires a bit of VCL::

  sub vcl_recv {
	  if (req.request == "BAN") {
                  # Same ACL check as above:
		  if (!client.ip ~ purge) {
			  error 405 "Not allowed.";
		  }
		  ban("req.http.host == " + req.http.host +
		        "&& req.url == " + req.url);

		  # Throw a synthetic page so the
                  # request won't go to the backend.
		  error 200 "Ban added";
	  }
  }

This VCL sniplet enables Varnish to handle an HTTP BAN method, adding a
ban on the URL, including the host part.

