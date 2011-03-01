.. _tutorial-cookies:

Cookies
-------

Varnish will not cache a object coming from the backend with a
Set-Cookie header present. Also, if the client sends a Cookie header,
Varnish will bypass the cache and go directly to the backend.

This can be overly conservative. A lot of sites use Google Analytics
(GA) to analyze their traffic. GA sets a cookie to track you. This
cookie is used by the client side java script and is therefore of no
interest to the server. 

For a lot of web application it makes sense to completely disregard the
cookies unless you are accessing a special part of the web site. This
VCL snippet in vcl_recv will disregard cookies unless you are
accessing /admin/::

  if ( !( req.url ~ ^/admin/) ) {
    unset req.http.Cookie;
  }

Quite simple. If, however, you need to do something more complicated,
like removing one out of several cookies, things get
difficult. Unfortunately Varnish doesn't have good tools for
manipulating the Cookies. We have to use regular expressions to do the
work. If you are familiar with regular expressions you'll understand
what's going on. If you don't, I suggest you either pick up a book on
the subject, read through the *pcrepattern* man page, or read through
one of many online guides.

Let me show you what Varnish Software uses. We use some cookies for
Google Analytics tracking and similar tools. The cookies are all set
and used by Javascript. Varnish and Drupal doesn't need to see those
cookies and since Varnish will cease caching of pages when the client
sends cookies we will discard these unnecessary cookies in VCL. 

In the following VCL we discard all cookies that start with a
underscore::

  // Remove has_js and Google Analytics __* cookies.
  set req.http.Cookie = regsuball(req.http.Cookie, "(^|;\s*)(_[_a-z]+|has_js)=[^;]*", "");
  // Remove a ";" prefix, if present.
  set req.http.Cookie = regsub(req.http.Cookie, "^;\s*", "");

Let me show you an example where we remove everything but the cookies
named COOKIE1 and COOKIE2 and you can marvel at it::

  sub vcl_recv {
    if (req.http.Cookie) {
      set req.http.Cookie = ";" req.http.Cookie;
      set req.http.Cookie = regsuball(req.http.Cookie, "; +", ";");
      set req.http.Cookie = regsuball(req.http.Cookie, ";(COOKIE1|COOKIE2)=", "; \1=");
      set req.http.Cookie = regsuball(req.http.Cookie, ";[^ ][^;]*", "");
      set req.http.Cookie = regsuball(req.http.Cookie, "^[; ]+|[; ]+$", "");

      if (req.http.Cookie == "") {
          remove req.http.Cookie;
      }
  }

The example is taken from the Varnish Wiki, where you can find other
scary examples of what can be done i VCL.
