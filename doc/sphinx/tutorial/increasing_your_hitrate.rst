.. _tutorial-increasing_your_hitrate:

Achieving a high hitrate
------------------------

Now that Varnish is up and running, and you can access your web
application through Varnish. Unless your application is specifically
written to work behind a web accelerator you'll probably need to do
some changes to either the configuration or the application in order
to get a high hitrate in Varnish.

Note that you need a tool to see what HTTP headers fly between you and
the web server. If you have Varnish the easiest might be to use
varnishlog, but sometimes a separate tool makes sense. Here are the
ones I use.

lwp-request
~~~~~~~~~~~

lwp-request is part of The World-Wide Web library for Perl. It's
couple of really basic programs that can execute a HTTP request and
give you the result. I use two programs, GET and HEAD.

vg.no was the first site to use Varnish and the people running Varnish
there are quite clueful. So its interesting to look at their HTTP
Headers. Lets send a GET requst for their home page.::

  $ GET -H 'Host: vg.no' -Used http://vg.no/ 
  GET http://www.vg.no/
  User-Agent: Rickzilla 1.0
  
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

OK. Let me explain what it does. GET usually send off HTTP 0.9
requests, which lack the Host header. So I add a Host header with the
-H option. -U print request headers, -s prints response status -e
prints repsonse headers and -d discards the actual content. We dont
really care about the content, only the headers.

As you can see VG ads quite a bit of information in their headers. A
lot of this information is added in their VCL.

So, to check whether a site sets cookies for a specific URL just do
``GET -Used http://example.com/ |grep Set-Cookie``

Firefox plugins
~~~~~~~~~~~~~~~

There are also a couple of great plugins for Firefox. Both *Live HTTP
Headers* and *Firebug* can show you what headers are beeing sent and
recieved.


HTTP Headers
------------

Varnish considers itself part of the actual webserver, since its under
your control. The role of *surrogate origin cache* is not really well
defined by the IETF so RFC 2616 doesn't always tell us what we should
do

Cache-control
~~~~~~~~~~~~~

The Cache-Control instructs caches how to handle the content. Varnish
cares about the *max-age* parameter and uses it to calculate the TTL
for an object. 

"Cache-Control: nocache" is ignored. See
:ref:`tutorial-increasing_your_hitrate-pragma:` for an example on how
to implement support.

Cookies
~~~~~~~

Varnish will not cache a object comming from the backend with a
Set-Cookie header present. Also, if the client sends a Cookie header,
Varnish will bypass the cache and go directly to the backend.

This can be overly conservative. A lot of sites use Google Analytics
(GA) to analyse their traffic. GA sets a cookie to track you. This
cookie is used by the client side java script and is therefore of no
interest to the server. 

For a lot of web application it makes sense to completly disregard the
cookies unless you are accessing a special part of the web site. This
VCL snipplet in vcl_recv will disregard cookies unless you are
accessing /admin/.::

  if ( !( req.url ~ ^/admin/) ) {
    unset http.Cookie;
  }

Quite simple. If, however, you need to do something more complicated,
like removing one out of several cookies, things get
difficult. Unfornunatly Varnish doesn't have good tools for
manipulating the Cookies. We have to use regular expressions to do the
work. Let me show you an example where we remove everything the the cookies named COOKIE1 and COOKIE2  and you can marvel at it.::

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

Vary
~~~~

The Vary header is sent by the web server to indicate what makes a
HTTP object Vary.

.. _tutorial-increasing_your_hitrate-pragma:

Pragma
~~~~~~


HTTP 1.0 server might send "Pragma: nocache". Varnish ignores this
header. You could easly add support for this header in VCL.

In vcl_fetch::

  if (beresp.http.Pragma ~ "nocache") {
     pass;
  }

Authentication
~~~~~~~~~~~~~~

Normalizing your namespace
--------------------------

.. _tutorial-increasing_your_hitrate-purging:

Purging
-------


HTTP Purges
~~~~~~~~~~~

Bans
~~~~



