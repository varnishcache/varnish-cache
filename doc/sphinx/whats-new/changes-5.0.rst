..
	Copyright (c) 2016 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _whatsnew_changes_5.0:

Changes in Varnish 5.0
======================

Varnish 5.0 changes some (mostly) internal APIs and adds some major new
features over Varnish 4.1.


Separate VCL files and VCL labels
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Varnish 5.0 supports jumping from the active VCL's ``vcl_recv{}`` to
another VCL via a VCL label.

The major use of this will probably be to have a separate VCL for
each domain/vhost, in order to untangle complex VCL files, but
it is not limited to this criteria, it would also be possible to
send all POSTs, all JPEG images or all traffic from a certain
IP range to a separate VCL file.

VCL labels can also be used to give symbolic names to loaded VCL
configurations, so that operations personnel only need to know
about "normal", "weekend" and "emergency", and web developers
can update these as usual, without having to tell ops what the
new weekend VCL is called.


Very Experimental HTTP/2 support
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

We are in the process of adding HTTP/2 support to Varnish, but
the code is very green still - life happened.

But you can actually get a bit of traffic though it already, and
we hope to have it production ready for the next major release
(2017-03-15).

Varnish supports HTTP/1 -> 2 upgrade.  For political reasons,
no browsers support that, but tools like curl does.

For encrypted HTTP/2 traffic, put a SSL proxy in front of Varnish.

HTTP/2 support is disabled by default, to enable, set the ``http2``
feature bit.


The Shard Director
~~~~~~~~~~~~~~~~~~

We have added to the directors VMOD an overhauled version of a
director which was available as an out-of-tree VMOD under the name
VSLP for a couple of years: It's basically a better hash director,
which uses consistent hashing to provide improved stability of backend
node selection when the configuration and/or health state of backends
changes. There are several options to provide the shard key. The
rampup feature allows to take just-gone-healthy backends in production
smoothly, while the prewarm feature allows to prepare backends for
traffic which they would see if the primary backend for a certain key
went down.

It can be reconfigured dynamically (outside ``vcl_init{}``), but
different to our other directors, configuration is transactional: Any
series of backend changes must be concluded by a reconfigure call for
activation.


Hit-For-Pass is now actually Hit-For-Miss
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Almost since the beginning of time (2008), varnish has hit-for-pass:
It is basically a negative caching feature, putting into the cache
objects as markers saying "when you hit this, your request should be a
pass". The purpose is to selectively avoid the request coalescing
(waitinglist) feature, which is useful for cacheable content, but not
for uncacheable objects. If we did not have hit-for-pass, without
additional configuration in vcl_recv, requests to uncacheable content
would be sent to the backend serialized (one after the other).

As useful as this feature is, it has caused a lot of headaches to
varnish administrators along the lines of "why the *beep* doesn't
Varnish cache this": A hit-for-pass object stayed in cache for however
long its ttl dictated and prevented caching whenever it got hit ("for
that url" in most cases). In particular, as a pass object cannot be
turned into something cacheable retrospectively
(``beresp.uncacheable`` can be changed from ``false`` to ``true``, but
not the other way around), even responses which would have been
cacheable were not cached. So, when a hit-for-pass object got into
cache unintentionally, it had to be removed explicitly (using a ban or
purge).

We've changed this now:

A hit-for-pass object (we still call it like this in the docs, logging
and statistics) will now cause a cache-miss for all subsequent
requests, so if any backend response qualifies for caching, it will
get cached and subsequent requests will be hits.

In short: We've changed from "the uncacheable case wins" to "the
cacheable case wins" or from hit-for-pass to hit-for-miss.

The primary consequence which we are aware of at the time of this
release is caused be the fact that, to create cacheable objects, we
need to make backend requests unconditional (that is, remove the
``If-Modified-Since`` and ``If-None-Match headers``): For conditional
client requests on hit-for-pass objects, Varnish will now issue an
unconditional backend fetch and, for 200 responses, send a 304 or 200
response to the client as appropriate.

As of the time of this release we cannot say if this will remain the
final word on this topic, but we hope that it will mean an improvement
for most users of Varnish.


Ban Lurker Improvements
~~~~~~~~~~~~~~~~~~~~~~~

We have made the ban lurker even more efficient by example of some
real live situations with tens of thousands of bans using inefficient
regular expressions.

The new parameter ``ban_lurker_holdoff`` tells the ban lurker for how
long it should get out of the way when it could potentially slow down
lookups due to lock contention. Previously this was the same as
``ban_lurker_sleep``.

.. _whatsnew_changes_5.0_reqbody:

Request Body sent always / "cacheable POST"
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Previously, we would only send a request body for passed requests (and
for pipe mode, but this is special anyway and should be avoided).

Not so anymore, but the default behaviour has not changed:

Whenever a request has a body, it will get sent to the backend for a
cache miss (and pass, as before). This can be prevented by an
``unset bereq.body`` and the ``builtin.vcl`` removes the body for GET
requests because it is questionable if GET with a body is valid anyway
(but some applications use it).

So the often-requested ability to cache POST/PATCH/... is now available,
but not out-of-the-box:

* The ``builtin.vcl`` still contains a ``return(pass)`` for anything
  but a GET or HEAD because other HTTP methods, by definition, may cause
  state changes / side effects on backends. The application at hand
  should be understood well before caching of non-GET/non-HEAD is
  considered.

* For misses, core code still calls the equivalent of ``set
  bereq.method = "GET"`` before calling ``vcl_backend_fetch``, so to
  make a backend request with the original request method, it needs to
  be saved in ``vcl_recv`` and restored in ``vcl_backend_fetch``.

* Care should be taken to choose an appropriate cache key and/or Vary
  criteria. Adding the request body to the cache key is not possible
  with core varnish, but through a VMOD
  https://github.com/aondio/libvmod-bodyaccess

To summarize: You should know what you are doing when caching anything
but a GET or HEAD and without creating an appropriate cache key doing
so is almost guaranteed to be wrong.


ESI and Backend Request Coalescing ("waitinglist") Improvement
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Previously, ESI subrequests depending on objects being fetched from
the backed used polling, which typically added some ~5ms of processing
time to such subrequests and could lead to starvation effects in
extreme corner cases.

The waitinglist logic for ESI subrequests now uses condition variables
to trigger immediate continuation of ESI processing when an object
being waited for becomes available.


Backend PROXY protocol requests
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Are now supported through the ``.proxy_header`` attribute of the
backend definition.

Default VCL search path
~~~~~~~~~~~~~~~~~~~~~~~

For default builds, vcl files are now also being looked for under
``/usr/share/varnish/vcl`` if not found in ``/etc/varnish``.

For custom builds, the actual search path is
``${varnishconfdir}:${datarootdir}/varnish/vcl``


devicedetect.vcl
~~~~~~~~~~~~~~~~

The basic device detection vcl is now bundled with varnish.

varnishtest
~~~~~~~~~~~

* ``resp.msg`` renamed to ``resp.reason`` for consistency with vcl
* HTTP2 testing capabilities added
* default search path for executables and vmods added
* ``sema`` mechanism replaced by ``barrier``
* support for PROXY requests

misc
~~~~

Brief notes on other changes

* Added separate thread for object expiry
* The ESI parser is now more tolerant to some syntactic corner cases
* Reduced needless rushing of requests on the waitinglist
* ``varnishhist`` can now process backend requests and offers a timebend
  function to control the processing speed
* ``std.integer()`` can now also parse real numbers and truncates them
* ``std.log()`` now also works correctly during ``vcl_init{}``
* further improved stability when handling workspace overflows
* numerous vcl compiler improvements

News for VMOD authors
~~~~~~~~~~~~~~~~~~~~~

* It is now mandatory to have a description in the ``$Module`` line of
  a ``vcc`` file.

* vcl cli events (in particular, ``vcl_init{}`` /``vcl_fini{}``) now
  have a workspace and ``PRIV_TASK`` available for VMODs.

* ``PRIV_*`` now also work for object methods with unchanged scope.
  In particular, they are per VMOD and `not` per object - e.g. the
  same ``PRIV_TASK`` gets passed to object methods as to functions
  during a VCL task.

* varnish now provides a random number api, see vrnd.h

* vbm (variable size bitmaps) improved

* ``vmodtool.py`` for translating vcc files has been largely
  rewritten, there may still exist regressions which remained unnoticed

* ``vmodtool.py`` now requires at least Python 2.6

* New autoconf macros are available, they should greatly simplify build
  systems of out-of-tree VMODs.  They are implemented and documented in
  ``varnish.m4``, and the previous macros now live in ``varnish-legacy.m4``
  so existing VMODs should still build fine.
