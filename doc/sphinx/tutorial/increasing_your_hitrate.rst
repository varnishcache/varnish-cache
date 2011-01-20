.. _tutorial-increasing_your_hitrate:

Achieving a high hitrate
------------------------

Now that Varnish is up and running, and you can access your web
application through Varnish. Unless your application is specifically
written to work behind a web accelerator you'll probably need to do
some changes to either the configuration or the application in order
to get a high hit rate in Varnish.

Varnish will not cache your data unless it's absolutely sure it is
safe to do so. So, for you to understand how Varnish decides if and
how to cache a page I'll guide you through a couple of tools that you
will find useful.

Note that you need a tool to see what HTTP headers fly between you and
the web server. If you have Varnish the easiest is to use varnishlog
and varnishtop but sometimes a client-side tool makes sense. Here are
the ones I use.

Tool: varnishtop
~~~~~~~~~~~~~~~~

You can use varnishtop to identify what URLs are hitting the backend
the most. ``varnishtop -i txurl`` is an essential command. You can see
some other examples of varnishtop usage in :ref:`tutorial-statistics`.


Tool: varnishlog
~~~~~~~~~~~~~~~~

When you have identified the an URL which is frequently sent to the
backend you can use varnishlog to have a look at the whole request.
``varnishlog -c -o /foo/bar`` will give the whole (-o) requests coming
from the client (-c) matching /foo/bar. 


Tool: lwp-request
~~~~~~~~~~~~~~~~~

lwp-request is part of The World-Wide Web library for Perl. It's a
couple of really basic programs that can execute an HTTP request and
give you the result. I mostly use two programs, GET and HEAD.

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

OK. Let me explain what it does. GET usually send off HTTP 0.9
requests, which lack the Host header. So I add a Host header with the
-H option. -U print request headers, -s prints response status, -e
prints response headers and -d discards the actual content. We don't
really care about the content, only the headers.

As you can see, VG adds quite a bit of information in their
headers. Some of the headers, like the X-Rick-Would-Never are specific
to vg.no and their somewhat odd sense of humour. Others, like the
X-VG-Webcache are for debugging purposes. 

So, to check whether a site sets cookies for a specific URL, just do::

  GET -Used http://example.com/ |grep ^Set-Cookie

Tool: Live HTTP Headers
~~~~~~~~~~~~~~~~~~~~~~~

There is also a plugin for Firefox. *Live HTTP Headers* can show you
what headers are being sent and recieved. Live HTTP Headers can be
found at https://addons.mozilla.org/en-US/firefox/addon/3829/ or by
googling "Live HTTP Headers".


The role of HTTP Headers
~~~~~~~~~~~~~~~~~~~~~~~~

Along with each HTTP request and reponse comes a bunch of headers
carrying metadata. Varnish will look at these headers to determine if
it is appropriate to cache the contents and how long Varnish can keep
the content.

Please note that when considering these headers Varnish actually
considers itself *part of* the actual webserver. The rationale being
that both are under your control. 

The term *surrogate origin cache* is not really well defined by the
IETF so RFC 2616 so the various ways Varnish works might differ from
your expectations.

Let's take a look at the important headers you should be aware of:

Cache-Control
~~~~~~~~~~~~~

The Cache-Control instructs caches how to handle the content. Varnish
cares about the *max-age* parameter and uses it to calculate the TTL
for an object. 

"Cache-Control: nocache" is ignored but if you need this you can
easily add support for it.

So make sure use issue a Cache-Control header with a max-age
header. You can have a look at what Varnish Software's drupal server
issues::

  $ GET -Used http://www.varnish-software.com/|grep ^Cache-Control
  Cache-Control: public, max-age=600

Age
~~~

Varnish adds an Age header to indicate how long the object has been
kept inside Varnish. You can grep out Age from varnishlog like this::

  varnishlog -i TxHeader -I ^Age

Pragma
~~~~~~

HTTP 1.0 server might send "Pragma: nocache". Varnish ignores this
header. You could easily add support for this header in VCL.

In vcl_fetch::

  if (beresp.http.Pragma ~ "nocache") {
     pass;
  }

Authorization
~~~~~~~~~~~~~

If Varnish sees an Authorization header it will pass the request. If
this is not what you want you can unset the header.

Overriding the time-to-live (ttl)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Sometimes your backend will misbehave. It might, depending on your
setup, be easier to override the ttl in Varnish than to fix your
somewhat cumbersome backend. 

You need VCL to identify the objects you want and then you set the
beresp.ttl to whatever you want::

  sub vcl_fetch {
      if (req.url ~ "^/legacy_broken_cms/") {
          set beresp.ttl = 5d;
      }
  }


Normalizing your namespace
~~~~~~~~~~~~~~~~~~~~~~~~~~

Some sites are accessed via lots of
hostnames. http://www.varnish-software.com/,
http://varnish-software.com/ and http://varnishsoftware.com/ all point
at the same site. Since Varnish doesn't know they are different,
Varnish will cache different versions of every page for every
hostname. You can mitigate this in your web server configuration by
setting up redirects or by using the following VCL::

  if (req.http.host ~ "^(www.)?varnish-?software.com") {
    set req.http.host = "varnish-software.com";
  }


Ways of increasing your hitrate even more
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following chapters should give your ways of further increasing
your hitrate, especially the chapter on Cookies.

 * :ref:`tutorial-cookies`
 * :ref:`tutorial-vary`
 * :ref:`tutorial-purging`
 * :ref:`tutorial-esi`

