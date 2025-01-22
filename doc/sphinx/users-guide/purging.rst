..
	Copyright (c) 2012-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _users-guide-purging:


Purging and banning
-------------------

One of the most effective ways of increasing your hit ratio is to
increase the time-to-live (ttl) of your objects. But, as you're aware
of, in this twitterific day of age, serving content that is outdated is
bad for business.

The solution is to notify Varnish when there is fresh content
available. This can be done through three mechanisms. HTTP purging,
banning and forced cache misses. First, lets look at HTTP purging.


HTTP Purging
~~~~~~~~~~~~

A *purge* is what happens when you pick out an object from the cache
and discard it along with its variants. Usually a purge is invoked
through HTTP with the method `PURGE`.

An HTTP purge is similar to an HTTP GET request, except that the
*method* is `PURGE`. Actually you can call the method whatever you'd
like, but most people refer to this as purging. Squid, for example,
supports the same mechanism. In order to support purging in Varnish
you need the following VCL in place::

  acl purge {
	  "localhost";
	  "192.168.55.0"/24;
  }

  sub vcl_recv {
      	  # allow PURGE from localhost and 192.168.55...

	  if (req.method == "PURGE") {
		  if (!client.ip ~ purge) {
			  return(synth(405,"Not allowed."));
		  }
		  return (purge);
	  }
  }

As you can see we have used a new action - return(purge). This ends
execution of vcl_recv and jumps to vcl_hash. This is just like we
handle a regular request. When vcl_hash calls return(lookup) Varnish
will purge the object and then call vcl_purge. Here you have the
option of adding any particular actions you want Varnish to take once
it has purge the object.

So for example.com to invalidate their front page they would call out
to Varnish like this::

  PURGE / HTTP/1.0
  Host: example.com

And Varnish would then discard the front page. This will remove all
variants as defined by Vary.

Bans
~~~~

There is another way to invalidate content: Bans. You can think of
bans as a sort of a filter on objects already in the cache. You ``ban``
certain content from being served from your cache. You can ban
content based on any metadata we have.
A ban will only work on objects already in the cache, it does not
prevent new content from entering the cache or being served.

Support for bans is built into Varnish and available in the CLI
interface. To ban every png object belonging on example.com, issue
the following command from the shell::

  varnishadm ban req.http.host == example.com '&&' req.url '~' '\\.png$'

See :ref:`std.ban()` for details on the syntax of ban expressions. In
particular, note that in the example given above, the quotes are
required for execution from the shell and escaping the backslash in
the regular expression is required by the Varnish cli interface.

Bans are checked when we hit an object in the cache, but before we
deliver it. *An object is only checked against newer bans*.

During lookup, object variants that may not satisfy the current request
are also tested against the ban list, which means that a ban may also
hit a non matching variant.

However, the parameter `ban_any_variant` can be used to limit the number
of possibly non matching variants that are checked against the ban list during
lookup for a particular request. This means that at most `ban_any_variant`
variants will be evaluated, and possibly evicted, before looking for matching
variants. A value of 0 means that every request would only evaluate bans
against matching variants. In contrast, a value that is too high may cause a
request to evaluate all variants against all active bans, which can add
significant delays for configurations having a large number of variants
and/or bans.

In the next major release of varnish (8.0), the default value of
`ban_any_variant` will be set to 0.

Bans that only match against `obj.*` are also processed by a background
worker threads called the `ban lurker`. The `ban lurker` will walk the
heap and try to match objects and will evict the matching objects. How
aggressive the `ban lurker` is can be controlled by the parameter
'ban_lurker_sleep'. The `ban lurker` can be disabled by setting
'ban_lurker_sleep' to 0.

.. XXX: sample here? benc

Bans that are older than the oldest objects in the cache are discarded
without evaluation. If you have a lot of objects with long TTL, that
are seldom accessed, you might accumulate a lot of bans. This might
impact CPU usage and thereby performance.

You can also add bans to Varnish via HTTP. Doing so requires a bit of VCL::

  import std;

  sub vcl_recv {
	  if (req.method == "BAN") {
		  # Same ACL check as above:
		  if (!client.ip ~ purge) {
			  return(synth(403, "Not allowed."));
		  }
		  if (std.ban("req.http.host == " + req.http.host +
		      " && req.url == " + req.url)) {
			  return(synth(200, "Ban added"));
		  } else {
			  # return ban error in 400 response
			  return(synth(400, std.ban_error()));
		  }
	  }
  }

This VCL stanza enables Varnish to handle a `HTTP BAN` method, adding a
ban on the URL, including the host part.

The `ban lurker` can help you keep the ban list at a manageable size, so
we recommend that you avoid using `req.*` in your bans, as the request
object is not available in the `ban lurker` thread.

You can use the following template to write `ban lurker` friendly bans::

  import std;

  sub vcl_backend_response {
	  set beresp.http.url = bereq.url;
  }

  sub vcl_deliver {
	  unset resp.http.url; # Optional
  }

  sub vcl_recv {
	  if (req.method == "BAN") {
		  # Same ACL check as above:
		  if (!client.ip ~ purge) {
			  return(synth(403, "Not allowed."));
		  }
		  # Assumes req.url is a regex. This might be a bit too simple
		  if (std.ban("obj.http.url ~ " + req.url)) {
			  return(synth(200, "Ban added"));
		  } else {
			  # return ban error in 400 response
			  return(synth(400, std.ban_error()));
		  }
	  }
  }

To inspect the current ban list, issue the ``ban.list`` command in the CLI. This
will produce a status of all current bans::

  0xb75096d0 1318329475.377475    10      obj.http.url ~ test
  0xb7509610 1318329470.785875    20G     obj.http.url ~ test

The ban list contains the ID of the ban, the timestamp when the ban
entered the ban list. A count of the objects that has reached this point
in the ban list, optionally postfixed with a 'G' for "Gone", if the ban
is no longer valid.  Finally, the ban expression is listed. Notice
that durations are not necessarily expressed in the originally given
unit, for instance ``7d`` will get turned into ``1w``.

The ban can be marked as "Gone" if it is a duplicate ban, but is still
kept in the list for optimization purposes.

Forcing a cache miss
~~~~~~~~~~~~~~~~~~~~

The final way to invalidate an object is a method that allows you to
refresh an object by forcing a `hash miss` for a single request. If you set
'req.hash_always_miss' to true, Varnish will miss the current object in the
cache, thus forcing a fetch from the backend. This can in turn add the
freshly fetched object to the cache, thus overriding the current one. The
old object will stay in the cache until ttl expires or it is evicted by
some other means.

