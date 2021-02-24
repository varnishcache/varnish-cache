..
	Copyright (c) 2012-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _tutorial-intro:

Varnish: The beef in the sandwich
---------------------------------

You may have heard the term "web-delivery-sandwich" used in relation to
Varnish, and it is a pretty apt metafor::


       ┌─────────┐
       │ browser │
       └─────────┘                                            ┌─────────┐
                  \                                          ┌─────────┐│
       ┌─────┐     ╔═════════╗    ┌─────┐    ┌─────────┐    ┌─────────┐│┘
       │ app │ --- ║ Network ║ -- │ TLS │ -- │ Varnish │ -- │ Backend │┘
       └─────┘     ╚═════════╝    └─────┘    └─────────┘    └─────────┘
                   /               
       ┌────────────┐
       │ API-client │
       └────────────┘

The top layer of the sandwich, 'TLS' is responsible for handling
the TLS ("https") encryption, which means it must have access to
the cryptographic certificate which authenticates your website.

The bottom layer of the sandwich are your webservers, CDNs,
API-servers, business backend systems and all the other sources for
your web-content.

Varnish goes in the middle, where it provides caching, policy,
analytics, visibility and mitigation for your webtraffic.

How Varnish works
-----------------

For each and every request, Varnish runs through the 'VCL' program
to decide what should happen:  Which backend has this content, how
long time can we cache it, is it accessible for this request, should
it be redirected elsewhere and so on.  If that particular backend
is down, varnish can find another or substitute different content
until it comes back up.

Your first VCL program will probably be trivial, for instance just
splitting the traffic between two different backend servers::

    sub vcl_recv {
       if (req.url ~ "^/wiki") {
           set req.backend_hint = wiki_server;
       } else {
           set req.backend_hint = wordpress_server;
       }
    }

When you load the VCL program into Varnish, it is compiled into
a C-program which is compiled into a shared library, which varnish
then loads and calls into, therefore VCL code is *fast*.

Everything Varnish does is recorded in 'VSL' log records which can
be examined and monitored in real time or recorded for later use
in native or NCSA format, and when we say 'everything' we mean
*everything*::

    *   << Request  >> 318737    
    -   Begin          req 318736 rxreq
    -   Timestamp      Start: 1612787907.221931 0.000000 0.000000
    -   Timestamp      Req: 1612787907.221931 0.000000 0.000000
    -   VCL_use        boot
    -   ReqStart       192.0.2.24 39698 a1
    -   ReqMethod      GET
    -   ReqURL         /vmods/
    -   ReqProtocol    HTTP/1.1
    -   ReqHeader      Host: varnish-cache.org
    -   ReqHeader      Accept: text/html, application/rss+xml, […]
    -   ReqHeader      Accept-Encoding: gzip,deflate
    -   ReqHeader      Connection: close
    -   ReqHeader      User-Agent: Mozilla/5.0 […]
    -   ReqHeader      X-Forwarded-For: 192.0.2.24
    -   VCL_call       RECV
    -   VCL_acl        NO_MATCH bad_guys
    -   VCL_return     hash
    […]

These `VSL` log records are written to a circular buffer in shared
memory, from where other programs can subscribe to them via a supported
API.  One such program is `varnishncsa` which produces NCSA-style log
records::

	192.0.2.24 - - [08/Feb/2021:12:42:35 +0000] "GET http://vmods/ HTTP/1.1" 200 0 […]

Varnish is also engineered for uptime, it is not necessary to restart
varnish to change the VCL program, in fact, multiple VCL programs can be
loaded at the same time and you can switch between them instantly.

Caching with Varnish
--------------------

When Varnish receives a request, VCL can decide to look for a
reusable answer in the cache, if there is one, that becomes one
less request to put load on your backend applications database.
Cache-hits take less than a millisecond, often mere microseconds,
to deliver.

If there is nothing usable in the cache, the answer from the backend
can, again under VCL control, be put in the cache for some amount
of time, so future requests for the same object can find it there.

Varnish understands the `Cache-Control` HTTP header if your backend
server sends one, but ultimately the VCL program makes the decision
to cache and how long, and if you want to send a different `Cache-Control`
header to the clients, VCL can do that too.

Content Composition with Varnish
--------------------------------

Varnish supports `ESI - Edge Side Includes` which makes it possible
to send responses to clients which are composed of different bits
from different backends, with the very important footnote that the
different bits can have very different caching policies.

With ESI a backend can tell varnish to edit the content of another
object into a HTML page::

    <H1>Todays Top News</H1>
    <ESI:include src="/topnews"/>

The `/topnews` request will be handled like every other request in
Varnish, VCL will decide if it can be cached, which backend should
supply it and so on, so even if the whole object in the example can
not be cached, for instance if the page is dynamic content for a
logged-in user, the `/topnews` object can be cached and can be
shared from the cache, between all users.

Content Policy with Varnish
---------------------------

Because VCL is in full control of every request, and because VCL
can be changed instantly on the fly, Varnish is a great tool to
implement both reactive and prescriptive content-policies.

Prescriptive content-policies can be everything from complying
with UN sanctions using IP number access lists over delivering
native language content to different clients to closing
access to employee web-mail in compliance with "Right to
disconnect" laws.

Varnish, and VCL is particular, are well suited to sort requests
and collect metrics for real-time A/B testing or during migrations
to a new backend system.

Reactive content-policies can be anything from blocking access to
an infected backend or fixing the URL from the QR code on the new
product, to extending caching times while the backend rebuilds the
database.

Varnish is general purpose
--------------------------

Varnish is written to run on modern UNIX-like operating systems:
Linux, FreeBSD, OS/X, OpenBSD, NetBSD, Solaris, OmniOs, SmartOS etc.

Varnish runs on any CPU architecture: i386, amd64, arm32, arm64,
mips, power, riscV, s390 - you name it.

Varnish can be deployed on dedicated hardware, in VMs, jails,
Containers, Cloud, as a service or any other way you may care for.

Unfortunately the `sudo make me a sandwich`_ feature is not ready yet,
so you will have to do that yourself but click on "Next topic" in the
navigation menu on the left and we'll tell you the recipe...

.. _sudo make me a sandwich: https://xkcd.com/149/
