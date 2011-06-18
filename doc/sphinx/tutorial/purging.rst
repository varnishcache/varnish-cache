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
deliver it. An object is only checked against newer bans. If you have
a lot of objects with long TTL in your cache you should be aware of a
potential performance impact of having many bans.

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

