..
	Copyright (c) 2012-2020 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _users-guide-increasing_your_hitrate:

Achieving a high hitrate
------------------------

Now that Varnish is up and running you can access your web application
through Varnish. Unless your application is specifically written to
work behind a web accelerator you'll probably need to do some
changes to either the configuration or the application in order to
get a high hitrate in Varnish.

Varnish will not cache your data unless it's absolutely sure it is
safe to do so. So, for you to understand how Varnish decides if and
how to cache a page, we'll guide you through a couple of tools that
you should find useful to understand what is happening in your
Varnish setup.

Note that you need a tool to see the HTTP headers that fly between
Varnish and the backend. On the Varnish server, the easiest way to do
this is to use :ref:`varnishlog(1)` and :ref:`varnishtop(1)` but
sometimes a client-side tool makes sense. Here are the ones we
commonly use.

Tool: varnishtop
~~~~~~~~~~~~~~~~

You can use varnishtop to identify what URLs are hitting the backend
the most. ``varnishtop -i BereqURL`` is an essential command, showing
you the top requests Varnish is sending to the backend. You can see some
other examples of :ref:`varnishtop(1)` usage in :ref:`users-guide-statistics`.


Tool: varnishlog
~~~~~~~~~~~~~~~~

When you have identified an URL which is frequently sent to the
backend you can use :ref:`varnishlog(1)` to have a look at the
request.  ``varnishlog -q 'ReqURL ~ "^/foo/bar"'`` will show you the
requests coming from the client matching `/foo/bar`.

For more information on how :ref:`varnishlog(1)` works please see
:ref:`users-guide-logging` or then man page.


Tool: lwp-request
~~~~~~~~~~~~~~~~~

`lwp-request` is tool that is a part of The World-Wide Web library
for Perl. It's a couple of really basic programs that can execute
an HTTP request and show you the result. We mostly use the two
programs, ``GET`` and ``HEAD``.

vg.no was the first site to use Varnish and the people running Varnish
there are quite clueful. So it's interesting to look at their HTTP
Headers. Let's send a GET request for their home page::

  $ GET -H 'Host: www.vg.no' -Used http://vg.no/
  GET http://vg.no/
  Host: www.vg.no
  User-Agent: lwp-request/5.834 libwww-perl/5.834

  200 OK
  Cache-Control: must-revalidate
  Refresh: 600
  Title: VG Nett - Forsiden - VG Nett
  X-Age: 463
  X-Cache: HIT
  X-Rick-Would-Never: Let you down
  X-VG-Jobb: http://www.finn.no/finn/job/fulltime/result?keyword=vg+multimedia Merk:HeaderNinja
  X-VG-Korken: http://www.youtube.com/watch?v=Fcj8CnD5188
  X-VG-WebCache: joanie
  X-VG-WebServer: leon

OK. Lets look at what ``GET`` does. ``GET`` usually sends off HTTP 0.9
requests, which lack the 'Host' header. So we add a 'Host' header with the
'-H' option. '-U' print request headers, '-s' prints response status, '-e'
prints response headers and '-d' discards the actual content. We don't
really care about the content, only the headers.

As you can see, VG adds quite a bit of information in their
headers. Some of the headers, like the 'X-Rick-Would-Never' are specific
to vg.no and their somewhat odd sense of humour. Others, like the
'X-VG-Webcache' are for debugging purposes.

So, to check whether a site sets cookies for a specific URL, just do::

  GET -Used http://example.com/ |grep ^Set-Cookie

.. XXX:Missing explanation and sample for HEAD here. benc

Tool: Live HTTP Headers
~~~~~~~~~~~~~~~~~~~~~~~

There is also a plugin for Firefox called `Live HTTP Headers`. This
plugin can show you what headers are being sent and received.
`Live HTTP Headers` can be found at
https://addons.mozilla.org/en-US/firefox/addon/3829/ or by googling
"Live HTTP Headers".


The role of HTTP Headers
------------------------

Along with each HTTP request and response comes a bunch of headers
carrying metadata. Varnish will look at these headers to determine if
it is appropriate to cache the contents and how long Varnish can keep
the content cached.

Please note that when Varnish considers these headers Varnish actually
considers itself *part of* the actual webserver. The rationale being
that both are under your control.

The term *surrogate origin cache* is not really well defined by the
IETF or RFC 2616 so the various ways Varnish works might differ from
your expectations.

Let's take a look at the important headers you should be aware of:

.. _users-guide-cookies:

Cookies
~~~~~~~

Varnish will, in the default configuration, not cache an object coming
from the backend with a 'Set-Cookie' header present. Also, if the client
sends a Cookie header, Varnish will bypass the cache and go directly to
the backend.

This can be overly conservative. A lot of sites use Google Analytics
(GA) to analyze their traffic. GA sets a cookie to track you. This
cookie is used by the client side javascript and is therefore of no
interest to the server.

Cookies from the client
+++++++++++++++++++++++

For a lot of web applications it makes sense to completely disregard the
cookies unless you are accessing a special part of the web site. This
VCL snippet in `vcl_recv` will disregard cookies unless you are
accessing `/admin/`::

    if (!(req.url ~ "^/admin/")) {
        unset req.http.Cookie;
    }

Quite simple. If, however, you need to do something more complicated,
like removing one out of several cookies, things get
difficult. Unfortunately Varnish doesn't have good tools for
manipulating the Cookies. We have to use regular expressions to do the
work. If you are familiar with regular expressions you'll understand
whats going on. If you aren't we recommend that you either pick up a book on
the subject, read through the *pcre2pattern* man page, or read through
one of many online guides.

Lets use the Varnish Software (VS) web as an example here. Very
simplified the setup VS uses can be described as a Drupal-based
backend with a Varnish cache in front. VS uses some cookies for
Google Analytics tracking and similar tools. The cookies are all
set and used by JavaScript. Varnish and Drupal doesn't need to see
those cookies and since Varnish will cease caching of pages when
the client sends cookies Varnish will discard these unnecessary
cookies in VCL.

In the following VCL we discard all cookies that start with an
underscore::

    # Remove has_js and Google Analytics __* cookies.
    set req.http.Cookie = regsuball(req.http.Cookie, "(^|;\s*)(_[_a-z]+|has_js)=[^;]*", "");
    # Remove a ";" prefix, if present.
    set req.http.Cookie = regsub(req.http.Cookie, "^;\s*", "");

Lets look at an example where we remove everything except the
cookies named "COOKIE1" and "COOKIE2" and you can marvel at the "beauty" of it::

    sub vcl_recv {
        if (req.http.Cookie) {
            set req.http.Cookie = ";" + req.http.Cookie;
            set req.http.Cookie = regsuball(req.http.Cookie, "; +", ";");
            set req.http.Cookie = regsuball(req.http.Cookie, ";(COOKIE1|COOKIE2)=", "; \1=");
            set req.http.Cookie = regsuball(req.http.Cookie, ";[^ ][^;]*", "");
            set req.http.Cookie = regsuball(req.http.Cookie, "^[; ]+|[; ]+$", "");

            if (req.http.Cookie == "") {
                unset req.http.Cookie;
            }
        }
    }

A somewhat simpler example that can accomplish almost the same functionality can be
found below. Instead of filtering out "other" cookies it instead picks out
"the one" cookie that is needed, copies it to another header and then
copies it back to the request, deleting the original cookie header.

.. XXX:Verify correctness of request above! benc

::

    sub vcl_recv {
        # save the original cookie header so we can mangle it
        set req.http.X-Varnish-PHP_SID = req.http.Cookie;
        # using a capturing sub pattern, extract the continuous string of
        # alphanumerics that immediately follows "PHPSESSID="
        set req.http.X-Varnish-PHP_SID =
           regsuball(req.http.X-Varnish-PHP_SID, ";? ?PHPSESSID=([a-zA-Z0-9]+)( |;| ;).*","\1");
        set req.http.Cookie = req.X-Varnish-PHP_SID;
        unset req.X-Varnish-PHP_SID;
    }

There are other scary examples of what can be done in VCL in the
Varnish Cache Wiki.

.. XXX:Missing link here.


Cookies coming from the backend
+++++++++++++++++++++++++++++++

If your backend server sets a cookie using the 'Set-Cookie' header
Varnish will not cache the page when using the default configuration.
A `hit-for-miss` object (see :ref:`vcl_actions`) is
created.  So, if the backend server acts silly and sets unwanted
cookies just unset the 'Set-Cookie' header and all should be fine.


Cache-Control
~~~~~~~~~~~~~

The 'Cache-Control' header instructs caches how to handle the content. Varnish
cares about the *max-age* parameter and uses it to calculate the TTL
for an object.

So make sure you issue a 'Cache-Control' header with a max-age
header. You can have a look at what Varnish Software's Drupal server
issues::

  $ GET -Used http://www.varnish-software.com/|grep ^Cache-Control
  Cache-Control: public, max-age=600

Age
~~~

Varnish adds an 'Age' header to indicate how long the object has been
kept inside Varnish. You can grep out 'Age' from :ref:`varnishlog(1)`
with ``varnishlog -I RespHeader:^Age``.

Pragma
~~~~~~

An HTTP 1.0 server might send the header ``Pragma: nocache``. Varnish ignores this
header. You could easily add support for this header in VCL.

In `vcl_backend_response`::

    if (beresp.http.Pragma ~ "nocache") {
        set beresp.uncacheable = true;
        set beresp.ttl = 120s; # how long not to cache this url.
    }

Authorization
~~~~~~~~~~~~~

If Varnish sees an 'Authorization' header it will pass the request. If
this is not what you want you can unset the header.

Overriding the time-to-live (TTL)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Sometimes your backend will misbehave. It might, depending on your
setup, be easier to override the TTL in Varnish then to fix your
somewhat cumbersome backend.

You need VCL to identify the objects you want and then you set the
'beresp.ttl' to whatever you want::

    sub vcl_backend_response {
        if (bereq.url ~ "^/legacy_broken_cms/") {
            set beresp.ttl = 5d;
        }
    }

This example will set the TTL to 5 days for the old legacy stuff on
your site.

Forcing caching for certain requests and certain responses
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Since you still might have this cumbersome backend that isn't very friendly
to work with you might want to override more stuff in Varnish. We
recommend that you rely as much as you can on the default caching
rules. It is perfectly easy to force Varnish to lookup an object in
the cache but it isn't really recommended.


Normalizing your namespace
~~~~~~~~~~~~~~~~~~~~~~~~~~

Some sites are accessed via lots of hostnames.
http://www.varnish-software.com/, http://varnish-software.com/ and
http://varnishsoftware.com/ all point at the same site. Since Varnish
doesn't know they are the same, Varnish will cache different versions of
every page for every hostname. You can mitigate this in your web server
configuration by setting up redirects or by using the following VCL::

    if (req.http.host ~ "(?i)^(www.)?varnish-?software.com") {
        set req.http.host = "varnish-software.com";
    }


.. _users-guide-vary:

HTTP Vary
---------

*HTTP Vary is not a trivial concept. It is by far the most misunderstood
HTTP header.*

A lot of the response headers tell the client something about the
HTTP object being delivered. Clients can request different variants
of a HTTP object, based on their preference. Their preferences might
cover stuff like encoding or language. When a client prefers UK
English this is indicated through ``Accept-Language: en-uk``. Caches
need to keep these different variants apart and this is done through
the HTTP response header 'Vary'.

When a backend server issues a ``Vary: Accept-Language`` it tells
Varnish that its needs to cache a separate version for every different
Accept-Language that is coming from the clients.

If two clients say they accept the languages "en-us, en-uk" and
"da, de" respectively, Varnish will cache and serve two different
versions of the page if the backend indicated that Varnish needs
to vary on the 'Accept-Language' header.

Please note that the headers that 'Vary' refer to need to match
*exactly* for there to be a match. So Varnish will keep two copies
of a page if one of them was created for "en-us, en-uk" and the
other for "en-us,en-uk". Just the lack of a whitespace will force
Varnish to cache another version.

To achieve a high hitrate whilst using Vary is there therefore
crucial to normalize the headers the backends varies on. Remember,
just a difference in casing can force different cache entries.

The following VCL code will normalize the 'Accept-Language' header to
either "en", "de" or "fr", in this order of precedence::

    if (req.http.Accept-Language) {
        if (req.http.Accept-Language ~ "en") {
            set req.http.Accept-Language = "en";
        } elsif (req.http.Accept-Language ~ "de") {
            set req.http.Accept-Language = "de";
        } elsif (req.http.Accept-Language ~ "fr") {
            set req.http.Accept-Language = "fr";
        } else {
            # unknown language. Remove the accept-language header and
            # use the backend default.
            unset req.http.Accept-Language
        }
    }

Vary parse errors
~~~~~~~~~~~~~~~~~

Varnish will return a "503 internal server error" page when it fails
to parse the 'Vary' header, or if any of the client headers listed
in the Vary header exceeds the limit of 65k characters. An 'SLT_Error'
log entry is added in these cases.

Pitfall - Vary: User-Agent
~~~~~~~~~~~~~~~~~~~~~~~~~~

Some applications or application servers send ``Vary: User-Agent``
along with their content. This instructs Varnish to cache a separate
copy for every variation of 'User-Agent' there is and there are
plenty. Even a single patchlevel of the same browser will generate
at least 10 different 'User-Agent' headers based just on what
operating system they are running.

So if you *really* need to vary based on 'User-Agent' be sure to
normalize the header or your hit rate will suffer badly. Use the
above code as a template.

Cache misses
------------

When Varnish does not find an object for a request in the cache, then
by default it performs a fetch from the backend on the hypothesis that
the response might be cached. This has two important consequences:

* Concurrent backend requests for the same object are *coalesced* --
  only one fetch is executed at a time, and the other pending fetches
  wait for the result (unless you have brought about one of the states
  described below in :ref:`users-guide-uncacheable`). This is to
  prevent your backend from being hit by a "thundering herd" when the
  cached response has expired, or if it was never cached in the first
  place. If it turns out that the response to the first fetch is
  cached, then that cache object can be delivered immediately to other
  pending requests.

* The backend request for the cache miss cannot be conditional if
  Varnish does not have an object in the cache to validate; that is,
  it cannot contain the headers ``If-Modified-Since`` or
  ``If-None-Match``, which might cause the backend to return status
  "304 Not Modified" with no response body. Otherwise, there might not
  be a response to cache. If those headers were present in the client
  request, they are removed from the backend request.

By setting a grace time for cached objects (default 10 seconds), you
allow Varnish to serve stale content while waiting for coalesced fetches,
which are run asynchronously while the stale response is sent to the
client. For details see :ref:`users-guide-handling_misbehaving_servers`.

Although the headers for a conditional request are removed from the
backend fetch on a cache miss, Varnish may nevertheless respond to the
client request with "304 Not Modified" if the resulting response
allows it. At delivery time, if the client request had an
``If-None-Match`` header that matches the ``ETag`` header in the
response, or if the time in an ``If-Modified-Since`` request header is
equal to or later than the time in the ``Last-Modified`` response
header, Varnish will send the 304 response to the client. This happens
for both hits and misses.

Varnish can send conditional requests to the backend if it has an
object in the cache against which the validation can be performed. You
can ensure that an object is retained for this purpose by setting
``beresp.keep`` in ``vcl_backend_response``::

  sub vcl_backend_response {
    # Keep the response in cache for 4 hours if the response has
    # validating headers.
    if (beresp.http.ETag || beresp.http.Last-Modified) {
      set beresp.keep = 4h;
    }
  }

A stale object is not removed from the cache for the duration of
``beresp.keep`` after its TTL and grace time have expired. This will
increase the storage requirements for your cache, but if you have the
space, it might be worth it to keep stale objects that can be
validated for a fairly long time. If the backend can send a 304
response long after the TTL has expired, you save bandwidth on the
fetch and reduce pressure on the storage; if not, then it's no
different from any other cache miss.

If, however, you would prefer that backend fetches are not
conditional, just remove the If-* headers in ``vcl_backend_fetch``::

  sub vcl_backend_fetch {
    # To prevent conditional backend fetches.
    unset bereq.http.If-None-Match;
    unset bereq.http.If-Modified-Since;
  }

That should only be necessary if the conditional fetches are
problematic for the backend, for example if evaluating whether the
response is unchanged is too costly for the backend app, or if the
responses are just buggy. From the perspective of Varnish, 304
responses are clearly preferable; fetches with the empty response body
save bandwidth, and storage does not have to be allocated in the
cache, since the existing cache object is re-used.

To summarize, you can improve performance even in the case of cache
misses by:

* ensuring that cached objects have a grace time during which a stale
  object can be served to the client while fetches are performed in
  the background, and

* setting a keep time for cached objects that can be validated with
  a 304 response after they have gone stale.


.. _users-guide-uncacheable:

Uncacheable content
-------------------

Some responses cannot be cached, for various reasons. The content may
be personalized, depending on the content of the ``Cookie`` header, or
it might just be the sort of thing that is generated anew on each
request.  The cache can't help with that, but nevertheless there are
some decisions you can make that will help Varnish deal with
uncacheable responses in a way that is best for your requirements.

The issues to consider are:

* preventing request coalescing

* whether (and how soon) the response for the same object may become
  cacheable again

* whether you want to pass along ``If-Modified-Since`` and
  ``If-None-Match`` headers from the client request to the backend, to
  allow the backend to respond with status 304

Passing client requests
~~~~~~~~~~~~~~~~~~~~~~~

Depending on how your site works, you may be able to recognize a
client request for a response that cannot be cached, for example if
the URL matches certain patterns, or due to the contents of a request
header.  In that case, you can set the fetch to *pass* with
``return(pass)`` from ``vcl_recv``::

  sub vcl_recv {
    if (req.url ~ "^/this/is/personal/") {
      return(pass);
    }
  }

For passes there is no request coalescing. Since pass indicates that
the response will not be cacheable, there is no point in waiting for a
response that might be cached, and all pending fetches for the object
are concurrent. Otherwise, fetches waiting for an object that turns
out to be uncacheable after all may be serialized -- pending fetches
would wait for the first one, and when the result is not entered into
the cache, the next fetch begins while all of the others wait, and so
on.

When a request is passed, this can be recognized in the
``vcl_backend_*`` subroutines by the fact that ``bereq.uncacheable``
and ``beresp.uncachable`` are both true. The backend response will not
be cached, even if it fulfills conditions that otherwise would allow
it, for example if ``Cache-Control`` sets a positive TTL.

Pass is the default (that is, ``builtin.vcl`` calls ``return(pass)`` in
``vcl_recv``) if the client request meets these conditions:

* the request method is a standard HTTP/1.1 method, but not ``GET`` or
  ``HEAD``

* there is either a ``Cookie`` or an ``Authorization`` header, indicating
  that the response may be personalized

If you want to override the default, say if you are certain that the
response may be cacheable despite the presence of a Cookie, make sure
that a ``return`` gets called at the end of any path that may be taken
through your own ``vcl_recv``. But if you do that, no part of the
built-in ``vcl_recv`` gets executed; so take a close look at
``vcl_recv`` in ``builtin.vcl``, and duplicate any part of it that you
require in your own ``vcl_recv``.

As with cache hits and misses, Varnish decides to send a 304 response
to the client after a pass if the client request headers and the
response headers allow it. This might mean that Varnish will send a
304 response to the client even after the backend saw the same request
headers (``If-Modified-Since`` and/or ``If-None-Match``), but decided
not to respond with status 304, while nevertheless setting the
response headers ``ETag`` and/or ``Last-Modified`` so that 304 would
appear to be warranted. If you would prefer that Varnish doesn't do
that, then remove the If-* client request headers in ``vcl_pass``::

  sub vcl_pass {
    # To prevent 304 client responses after a pass.
    unset req.http.If-None-Match;
    unset req.http.If-Modified-Since;
  }

hit-for-miss
~~~~~~~~~~~~

You may not be able to recognize all requests for uncacheable content
in ``vcl_recv``. You might want to allow backends to determine their
own cacheability by setting the ``Cache-Control`` header, but that
cannot be seen until Varnish receives the backend response, so
``vcl_recv`` can't know about it.

By default, if a request is not passed and the backend response turns
out to be uncacheable, the cache object is set to "hit-for-miss", by
setting ``beresp.uncacheable`` to ``true`` in
``vcl_backend_response``.  A minimal object is saved in the cache, so
that the "hit-for-miss" state can be recognized on subsequent
lookups. (The cache is used to remember that the object is
uncacheable, for a limited time.)  In that case, no request coalescing
is performed, so that fetches can run concurrently. Otherwise, fetches
for hit-for-miss are just like cache misses, meaning that:

* the response may become cacheable on a later request, for example
  if it sets a positive TTL with ``Cache-Control``, and

* fetches cannot be conditional, so ``If-Modified-Since`` and
  ``If-None-Match`` headers are removed from the backend request.

When ``beresp.uncacheable`` is set to ``true``, then ``beresp.ttl``
determines how long the hit-for-miss state may last at most. The
hit-for-miss state ends after this period of time elapses, or if a
cacheable response is returned by the backend before it elapses (the
elapse of ``beresp.ttl`` just means that the minimal cache object
expires, like any other cache object expiration). If a cacheable
response is returned, then that object replaces the hit-for-miss
object, and subsequent requests for it will be cache hits. If no
cacheable response is returned before ``beresp.ttl`` elapses, then the
next request for that object will be an ordinary miss, and hence will
be subject to request coalescing.

When Varnish sees that it has hit a hit-for-miss object on a new
request, it executes ``vcl_miss``, so any custom VCL you have written
for cache misses will apply in the hit-for-miss case as well.

``builtin.vcl`` sets ``beresp.uncacheable`` to ``true``, invoking the
hit-for-miss state, under a number of conditions that indicate that
the response cannot be cached, for example if the TTL was computed to
be 0 or if there is a ``Set-Cookie`` header. ``beresp.ttl`` is set to
two minutes by ``builtin.vcl`` in this case, so that is how long
hit-for-miss lasts by default.

You can set ``beresp.uncacheable`` yourself if you need hit-for-miss
on other conditions::

  sub vcl_backend_response {
    if (beresp.http.X-This-Is == "personal") {
      set beresp.uncacheable = true;
    }
  }

Note that once ``beresp.uncacheable`` has been set to ``true`` it
cannot be set back to ``false``; attempts to do so in VCL are ignored.

Although the backend fetches are never conditional for hit-for-miss,
Varnish may decide (as in all other cases) to send a 304 response to
the client if the client request headers and response headers ``ETag``
or ``Last-Modified`` allow it. If you want to prevent that, remove
the If-* client request headers in ``vcl_miss``::

  sub vcl_miss {
    # To prevent 304 client responses on hit-for-miss.
    unset req.http.If-None-Match;
    unset req.http.If-Modified-Since;
  }

hit-for-pass
~~~~~~~~~~~~

A consequence of hit-for-miss is that backend fetches cannot be
conditional, since hit-for-miss allows subsequent responses to be
cacheable. This may be problematic for responses that are very large
and not cacheable, but may be validated with a 304 response. For
example, you may want clients to validate an object via the backend
every time, only sending the response when it has been changed.

For a situation like this, you can set an object to "hit-for-pass" with
``return(pass(DURATION))`` from ``vcl_backend_response``, where the
DURATION determines how long the hit-for-pass state lasts::

  sub vcl_backend_response {
    # Set hit-for-pass for two minutes if TTL is 0 and response headers
    # allow for validation.
    if (beresp.ttl <= 0s && (beresp.http.ETag || beresp.http.Last-Modified)) {
      return(pass(120s));
    }
  }

As with hit-for-miss, a minimal object is entered into the cache so
that the hit-for-pass state is recognized on subsequent requests. The
request is then processed as a pass, just as if ``vcl_recv`` had
returned pass.  This means that there is no request coalescing, and
that ``If-Modified-Since`` and ``If-None-Match`` headers in the client
request are passed along to the backend, so that the backend response
may be 304.

Varnish executes ``vcl_pass`` when it hits a hit-for-pass object. So
again, you can arrange for your own handling of both pass and
hit-for-pass with the same code in VCL.

If you want to prevent Varnish from sending conditional requests to
the backend, then remove the If-* headers from the backend request in
``vcl_backend_fetch``, as shown above for cache misses. And if you
want to prevent Varnish from deciding at delivery time to send a 304
response to the client based on the client request and response
headers, then remove the headers from the client request in
``vcl_pass``, as shown above for pass.

The hit-for-pass state ends when the "hit-for-pass TTL" given in the
``return`` statement elapses. As with passes, the response to a
hit-for-pass fetch is never cached, even if it would otherwise fulfill
conditions for cacheability.  So unlike hit-for-miss, it is not
possible to end the hit-for-pass state ahead of time with a cacheable
response. After the "hit-for-pass TTL" elapses, the next request for
that object is handled as an ordinary miss.

It is possible to end the hit-for-pass state of a cache object by
setting ``req.hash_always_miss`` to ``true`` in ``vcl_recv`` for a
request that will hit the object (you'll have to write VCL that brings
that about). The request in which that happens is forced to be a cache
miss, and the state of the object afterwards depends on the
disposition of the backend response -- it may become a cache hit,
hit-for-miss, or may be set to hit-for-pass again.

hit-for-miss is the default treatment of uncacheable content. No part
of ``builtin.vcl`` invokes hit-for-pass, so if you need it, you have to
add the necessary ``return`` statement to your own VCL.
