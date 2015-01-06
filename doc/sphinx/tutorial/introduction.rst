.. _tutorial-intro:

The fundamentals of web proxy caching with Varnish
--------------------------------------------------

Varnish is a caching HTTP reverse proxy. It receives requests from clients
and tries to answer them from the cache. If Varnish cannot answer the
request from the cache it will forward the request to the backend,
fetch the response, store it in the cache and deliver it to the client.

When Varnish has a cached response ready it is typically delivered in a
matter of microseconds, two orders of magnitude faster than your typical
backend server, so you want to make sure to have Varnish answer as many
of the requests as possible directly from the cache.

Varnish decides whether it can store the content or not based on the
response it gets back from the backend. The backend can instruct Varnish
to cache the content with the HTTP response header `Cache-Control`. There
are a few conditions where Varnish will not cache, the most common one
being the use of cookies. Since cookies indicates a client-specific web
object, Varnish will by default not cache it.

This behaviour as most of Varnish functionality can be changed using
policies written in the Varnish Configuration Language (VCL). See
:ref:`users-guide-index` for more information on how to do that.

Performance
~~~~~~~~~~~

Varnish has a modern architecture and is written with performance
in mind.  It is usually bound by the speed of the network, effectively
turning performance into a non-issue. You get to focus on how your web
applications work and you can allow yourself, to some degree, to care
less about performance and scalability.

.. XXX:Not totally sure what the last sentence above means. benc

Flexibility
~~~~~~~~~~~

One of the key features of Varnish Cache, in addition to its performance,
is the flexibility of its configuration language, VCL. VCL enables you
to write policies on how incoming requests should be handled.

In such a policy you can decide what content you want to serve, from
where you want to get the content and how the request or response should
be altered.

Supported platforms
--------------------

Varnish is written to run on modern versions of Linux and FreeBSD and the
best experience is had on those platforms. Thanks to our contributors
it also runs on NetBSD, OpenBSD, OS X and various Solaris-descendants
like Oracle Solaris, OmniOS and SmartOS.

About the Varnish development process
-------------------------------------

Varnish is a community driven project. The development is overseen by
the Varnish Governing Board which currently consists of Poul-Henning Kamp
(Architect), Rogier Mulhuijzen (Fastly) and Lasse Karstensen (Varnish
Software).

Please see https://www.varnish-cache.org/trac/wiki/Contributing as a
starting point if you would like to contribute to Varnish.

Getting in touch
----------------

You can get in touch with us through many channels. For real time chat
you can reach us on IRC through the server irc.linpro.net on the #varnish
and #varnish-hacking channels.
There are two mailing lists available: one for user questions and one
for development discussions. See https://www.varnish-cache.org/lists
for information and signup.  There is also a web forum on the same site.

Now that you have a vague idea on what Varnish Cache is, let's see if we
can get it up and running.

.. XXX:The above three paragraphs are repetitive this is already handled in previous chapters. The only new information is Governing Board which could be moved to the introduction and the paragraphs scrapped. benc
