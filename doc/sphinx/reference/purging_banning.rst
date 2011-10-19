%%%%%%%%%%%%%%%%%%%%%%%%%%%
Purging and banning content
%%%%%%%%%%%%%%%%%%%%%%%%%%%

Varnish has three ways to invalidate content in varnish. You can either purge an object and all the variants of it from the cache, or you can prevent past versions of an object from being served by banning it. Finally, you can force a cache miss to force a backend fetch and override an object in the cache.

Purging
=======

To purge an object, you need to access that object explicitly, which is why the purge method is only available in VCL in the methods `vcl_hit` and `vcl_miss`. Purging is available in `vcl_miss` to allow for purging of all other variants of this object, even when this is particular request didn't hit a variant. Purging explicitly evicts that object and all variants from the cache immediately. An example implementation of the HTTP PURGE method in VCL::

  acl purgers {
    "localhost";
    "192.0.2.1"/24;
  }

  sub vcl_recv {
    if (req.request == "PURGE") {
      if (!client.ip ~ purgers) {
        error 405 "Not allowed.";
      }
      return(lookup);
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

Banning
=======

Banning prevents varnish from serving all matching objects in the cache at the time of the ban. Banning is not a permanent operation, it is only used to invalidate and evict content already in the cache. It does not immediately evict objects from the cache, but will compare all future hits to this ban, and evict the objects if they match. When a ban has been matched against all objects in the cache, or when all objects in the cache is newer than the ban, it is deleted.

Bans are added to the ban list using the `ban` CLI command or the `ban()` VCL method. They both take a ban expression that matches the objects that should be banned.

Varnish also does duplicate ban detection if `ban_dups` is enabled.

Ban Expressions
---------------

A ban expression is a list of conditions that needs to be fulfilled to invalidate content. You can match the content of the following variables using equality comparison or regular expressions:

 * req.url
 * req.http.*
 * obj.http.*

Conditions are combined using logical and: &&

To ban any content served from an Apache backend, you could use this expression::

 obj.http.Server ~ Apache

To ban a particular URL and hostname::

 req.url == /this/url && req.http.Host == example.com

Banning From CLI
----------------

To ban from CLI, use the ban or the ban.url commands::

 ban obj.http.Server ~ Apache
 ban.url /images/.*

ban.url is equivelant to "ban req.url ~"

Banning From VCL
----------------

To ban from VCL, use the ban() or ban_url() functions. You can use the full arsenal of varnish string manipulation functions to build your ban expression. For example to let users execute requests that purge based on regular expressions::

 sub vcl_recv {
  if (req.url ~ "^/purgere/") {
   if (!client.ip ~ purge) {
    error 405 "Not allowed.";
   }
   set req.url = regsub(req.url, "^/purgere/", "/");
   ban("obj.http.x-url ~ " req.url);
   error 200 "Banned.";
  }
 }

The Ban List
------------

The ban list can be inspected via the CLI command ban.list.

Example output::

  0xb75096d0 1318329475.377475    10      obj.http.x-url ~ test
  0xb7509610 1318329470.785875    20G     obj.http.x-url ~ test

The ban list contains the ID of the ban, the timestamp when the ban entered the ban list. A count of the objects that has reached this point in the ban list, optionally postfixed with a 'G' for "Gone", if the ban is no longer valid. Finally, the ban expression is listed. The ban can be marked as Gone if it is a duplicate ban, but is still kept in the list for optimization purposes.

The Ban Lurker
--------------

Since a ban needs to be be matched against all objects in the cache, one way to speed up the eviction process is to enable the ban lurker. The ban lurker will walk the cache and match all objects to the bans in the ban list, and evict matching objects. The ban lurker is enabled by setting `ban_lurker_sleep` to a value above 0.

Since Varnish 3.0, the ban lurker is enabled by default.

Writing Ban Lurker Friendly Bans
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To fully utilize the ban lurker, bans need to be written without the use of any req.* parameters, since there is no request to match against when the ban lurker walks the cache.

A simple mode to avoid req.* in bans is to add headers to the cached object containing the parts of the request on which you want to ban, e.g.::

  sub vcl_fetch {
      set obj.http.x-url = req.url;
  }

  sub vcl_deliver {
      unset resp.http.x-url; # Optional
  }

  sub vcl_recv {
      if (req.request == "PURGE") {
            if (client.ip !~ purgers) {
                error 401 "Not allowed";
             }
            purge("obj.http.x-url ~ " req.url); # Assumes req.url is a regex. This might be a bit too simple
      }
  }

req.hash_always_miss
====================

The final way to invalidate an object is a method that allows you to refresh an object by forcing a hash miss for a single request. If you set `req.hash_always_miss` to true, varnish will miss the current object in the cache, thus forcing a fetch from the backend. This can in turn add the freshly fetched object to the cache, thus overriding the current one. The old object will stay in the cache until ttl expires or it is evicted by some other means.
