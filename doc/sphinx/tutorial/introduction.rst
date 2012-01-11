.. _tutorial-intro:

What is Varnish?
----------------

Varnish Cache is a Varnish Cache is a web application accelerator also
known as a caching HTTP reverse proxy. You install it in front of any
server that speaks HTTP and configure it to cache the
contents. Varnish Cache is really, really fast. It typically speeds up
delivery with a factor of 300 - 1000x, depending on your architecture.


Performance
~~~~~~~~~~~

Varnish performs really, really well. It is usually bound by the speed
of the network, effectivly turning performance into a non-issue. We've
seen Varnish delivering 20 Gbps on regular off-the-shelf hardware.

Flexibility
~~~~~~~~~~~

One of the key features of Varnish Cache, in addition to it's
performance, is the flexibility of it's configuration language,
VCL. VCL enables you to write policies on how incoming requests should
be handled. In such a policy you can decide what content you want to
serve, from where you want to get the content and how the request or
response should be altered. You can read more about this in our
tutorial.


Supported plattforms
~~~~~~~~~~~~~~~~~~~~

Varnish is written to run on modern versions of Linux and FreeBSD and
the best experience is had on those plattforms. Thanks to our
contributors it also runs on NetBSD, OpenBSD and OS X.
