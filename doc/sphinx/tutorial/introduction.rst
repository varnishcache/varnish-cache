.. _tutorial-intro:

The fundamentals of web proxy caching with Varnish
--------------------------------------------------

Varnish is a caching HTTP reverse proxy. It recieves requests from
clients and tries to answer them from its cache. If it cannot answer
the request from its cache it will forward the request to the backend,
fetch the response, store it and deliver it to the client.

Varnish decides whether it can store the content or not based on the
response it's gets back from the backend. The backend can instruct
Varnish to cache the content with the HTTP response header
Cache-Control.

Varnish will be very careful when it encounters cookies, either coming
from the client or from the origin server. When Varnish sees a
Set-Cookie header on a response it decides that the object is not
cacheable. When there is a Cookie header in the request it will also
refuse to serve a cached object and rather ask the backend for version
of the object that is tailored to the request.

This behaviour and most other behaviour can be changed using policies
written in the Varnish Configuration Language. See the Users Guide
for more information on how to do that.


Performance
~~~~~~~~~~~

Varnish has a modern architecture and is written with performance in
mind.  It is usually bound by the speed of the network, effectivly
turning performance into a non-issue. You get to focus on how your web
application work and you can allow yourself, to some degree, to care
less about performance and scalability.

Flexibility
~~~~~~~~~~~

One of the key features of Varnish Cache, in addition to it's
performance, is the flexibility of it's configuration language,
VCL. VCL enables you to write policies on how incoming requests should
be handled. 

In such a policy you can decide what content you want to serve, from
where you want to get the content and how the request or response
should be altered. 

Supported platforms
--------------------

Varnish is written to run on modern versions of Linux and FreeBSD and
the best experience is had on those platforms. Thanks to our
contributors it also runs on NetBSD, OpenBSD and OS X.

About the Varnish development process
-------------------------------------

Varnish is a community driven project. The development is overseen by
the Varnish Governing Board which currently consist of Poul-Henning
Kamp (Architect), Rogier Mulhuijzen (Fastly) and Kristian Lyngstøl
(Varnish Software).

Getting in touch
----------------

You can get in touch with us trough many channels. For real time chat
you can reach us on IRC trough the server irc.linpro.net on the
#varnish and #varnish-hacking channels.
The are two mailing lists available. One for user questions and one
for development discussions. See varnish-cache.org/mailinglist for
information and signup.  There is also a web forum on the same site.

Now that you have a vague idea on what Varnish Cache is, let see if we
can get it up and running.
