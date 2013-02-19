.. _tutorial-intro:

What is Varnish?
----------------

Varnish Cache is a web application accelerator. It can also be called
a HTTP reverse proxy. It sits in front of a web server, accepts HTTP
requests and tries to answer those by looking them up in it's
cache. If it can't answer the request from cache it will hand the
request over to a backend server.

You can say that Varnish Cache is a specialized key-value store that
stores HTTP responses. These are looked up with HTTP requests. If a
match is found the content is delivered.

Varnish Cache will typically look up a response by inspecting the HTTP
Host header together with the URL.  Varnish Cache maintains an index,
a hash, with all the host+url combinations that are kept in the cache.

Some times Varnish will refuse to store the content in it's
cache. This might be because the HTTP reponse has metadata that
disables cacheing or that there might be a cookie involved. Varnish,
in the default configuration, will refuse to cache content when there
are cookies involved because it has no idea if the content is derived
from the cookie or not.

All this behaviour can be changed using VCL. See the Users Guide for
more information on how to do that.


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
