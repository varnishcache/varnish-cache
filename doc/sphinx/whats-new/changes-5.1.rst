.. _whatsnew_changes_5.1:

Changes in Varnish 5.1
======================

We have a couple of new and interesting features in Varnish 5.1,
and we have a lot of smaller improvements and bugfixes all over
the place, in total we have made about 750 commits since Varnish 5.0,
so this is just some of the highlights.

Probably the biggest change in Varnish 5.1 is that a couple of very
significant contributors to Varnish have changed jobs, and therefore
stopped being active contributors to the Varnish Project.

Per Buer was one of the first people who realized that Varnish was
not just "Some program for a couple of Nordic newspapers",  and he
started the company Varnish Software, which is one of the major
sponsors of the Varnish Project.

Lasse Karstensen got roped into Varnish Software by Per, and in
addition to his other duties, he has taken care of the projects
system administration and release engineering for most of the 11
years we have been around now.

Per & Lasse:  "Thanks" doesn't even start to cover it, and we wish
you all the best for the future!

.. _whatsnew_clifile:

Startup CLI command file
~~~~~~~~~~~~~~~~~~~~~~~~

The new '-I cli_file' option to varnishd will make it much more
practical to use the VCL labels introduced in Varnish 5.0.

The cli commands in the file will be executed before the worker
process starts, so it could for instance contain::

	vcl.load panic /etc/varnish_panic.vcl
	vcl.load siteA0 /etc/varnish_siteA.vcl
	vcl.load siteB0 /etc/varnish_siteB.vcl
	vcl.load siteC0 /etc/varnish_siteC.vcl
	vcl.label siteA siteA0
	vcl.label siteB siteB0
	vcl.label siteC siteC0
	vcl.load main /etc/varnish_main.vcl
	vcl.use main

If a command in the file is prefixed with '-', failure will not
abort the startup.

Related to this change we have reordered the argument checking so
that argument problems are reported more consistently.

In case you didn't hear about them yet, labelling VCL programs
allows you to branch out to other VCLs in the main::vcl_recv{},
which in the above example could look like::

	sub vcl_recv {
	    if (req.http.host ~ "asite.example.com$") {
		return(vcl(siteA));
	    }
	    if (req.http.host ~ "bsite.example.com$") {
		return(vcl(siteB));
	    }
	    if (req.http.host ~ "csite.example.com$") {
		return(vcl(siteC));
	    }
	    // Main site processing ...
	}

Universal VCL return(fail)
~~~~~~~~~~~~~~~~~~~~~~~~~~

It is now possible to ``return(fail)`` anywhere in VCL,
including inside VMODs.  This will cause VCL processing
to terminate forthright.

In addition to ``return(fail)``, this mechanism will be
used to handle all failure conditions without a safe
fallback, for instance workspace exhaustion, too many
headers etc. (This is a work in progress, there is a
lot of code to review before we are done.)

In ``vcl_init{}`` failing causes the ``vcl.load`` to fail,
this is nothing new for this sub-routine.

A failure in any of the client side VCL methods (``vcl_recv{}``,
``vcl_hash{}`` ...) *except* ``vcl_synth{}``, sends the request
to ``vcl_synth{}`` with a 503, and reason "VCL failed".

A failure on the backend side (``vcl_backend_*{}``) causes the
fetch to fail.

(VMOD writers should use the new ``VRT_fail(ctx, format_string, ...)``
function which logs a SLT_VCL_Error record.)


Progress on HTTP/2 support
~~~~~~~~~~~~~~~~~~~~~~~~~~

HTTP/2 support is better than in 5.0, and is now enabled and survives
pretty well on our own varnish-cache.org website, but there are
still things missing, most notably windows and priority, which may
be fatal to more complex websites.

We expect HTTP/2 support to be production ready in the autumn 2017
release of Varnish-Cache, but that requires a testing and feedback
from real-world applications.

So if you have a chance to test our HTTP/2 code, by all means do
so, please report any crashes, bugs or other trouble back to us.

To enable HTTP/2 you need to ``param.set feature +http2`` but due
to internet-politics, you will only see HTTP/2 traffic if you have
an SSL proxy in front of Varnish which advertises HTTP2 with ALPN.

For the hitch SSL proxy, add the argument ``--alpn-protos="h2,http/1.1"``

.. _whatsnew_changes_5.1_hitpass:

Hit-For-Pass has returned
~~~~~~~~~~~~~~~~~~~~~~~~~

As hinted in :ref:`whatsnew_changes_5.0`, we have restored the
possibility of invoking the old hit-for-pass feature in VCL. The
treatment of uncacheable content that was new in version 5.0, which we
have taken to calling "hit-for-miss", remains the default. Now you can
choose hit-for-pass with ``return(pass(DURATION))`` from
``vcl_backend_response``, setting the duration of the hit-for-pass
state in the argument to ``pass``. For example:
``return(pass(120s))``.

To recap: when ``beresp.uncacheable`` is set to ``true`` in
``vcl_backend_response``, Varnish makes a note of it with a minimal
object in the cache, and finds that information again on the next
lookup for the same object. In essence, the cache is used to remember
that the last backend response was not cacheable. In that case,
Varnish proceeds as with a cache miss, so that the response may become
cacheable on subsequent requests. The difference is that Varnish does
not perform request coalescing, as it does for ordinary misses, when a
response has been marked uncacheable. For ordinary misses, when there
are requests pending for the same object at the same time, only one
fetch is executed at a time, since the response may be cached, in
which case the cached response may be used for the remaining
requests. But this is not done for "hit-for-miss" objects, since they
are known to have been uncacheable on the previous fetch.

``builtin.vcl`` sets ``beresp.uncacheable`` to ``true`` when a number
of conditions hold for a backend response that indicate that it should
not be cached, for example if the TTL has been determined to be 0
(perhaps due to a ``Cache-Control`` header), or if a ``Set-Cookie``
header is present in the response. So hit-for-miss is the default
for uncacheable backend responses.

A consequence of this is that fetches for uncacheable responses cannot
be conditional in the default case. That is, the backend request may
not include the headers ``If-Modified-Since`` or ``If-None-Match``,
which might cause the backend to return status "304 Not Modified" with
no response body. Since the response to a cache miss might be cached,
there has to be a body to cache, and this is true of hit-for-miss as
well. If either of those two headers were present in the client
request, they are removed from the backend request for a miss or
hit-for-miss.

Since conditional backend requests and the 304 response may be
critical to performance for non-cacheable content, especially if the
response body is large, we have made the old hit-for-pass feature
available again, with ``return(pass(DURATION))`` in VCL.

As with hit-for-miss, Varnish uses the cache to make a note of
hit-for-pass objects, and finds them again on subsequent lookups.  The
requests are then processed as for ordinary passes (``return(pass)``
from ``vcl_recv``) -- there is no request coalescing, and the response
will not be cached, even if it might have been otherwise.
``If-Modified-Since`` or ``If-None-Match`` headers in the client
request are passed along in the backend request, and a backend
response with status 304 and no body is passed back to the client.

The hit-for-pass state of an object lasts for the time given as the
DURATION in the previous return from ``vcl_backend_response``.  After
the "hit-for-pass TTL" elapses, the next request will be an ordinary
miss. So a hit-for-pass object cannot become cacheable again until
that much time has passed.

304 Not Modified responses after a pass
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Related to the previous topic, there has been a change in the way
Varnish handles a very specific case: deciding whether to send a "304
Not Modified" response to the client after a pass, when the backend
had the opportunity to send a 304 response, but chose not to by
sending a 200 response status instead.

Previously, Varnish went along with the backend when this happened,
sending the 200 response together with the response body to the
client. This was the case even if the backend set the response headers
``ETag`` and/or ``Last-Modified`` so that, when compared to the
request headers ``If-None-Match`` and ``If-Modified-Since``, a 304
response would seem to be warranted. Since those headers are passed
back to the client, the result could appear a bit odd from the
client's perspective -- the client used the request headers to ask if
the response was unmodified, and the response headers seem to indicate
that it wasn't, and yet the response status suggests that it was.

Now the decision to send a 304 client response status is made solely
at delivery time, based on the contents of the client request headers
and the headers in the response that Varnish is preparing to send,
regardless of whether the backend fetch was a pass. So Varnish may
send a 304 client response after a pass, even though the backend chose
not to, having seen the same request headers (if the response headers
permit it).

We made this change for consistency -- for hits, misses, hit-for-miss,
hit-for-pass, and now pass, the decision to send a 304 client response
is based solely on the contents of client request headers and the
response headers.

You can restore the previous behavior -- don't send a 304 client
response on pass if the backend didn't -- with VCL means, either by
removing the ``ETag`` or ``Last-Modified`` headers in
``vcl_backend_response``, or by removing the If-* client request
headers in ``vcl_pass``.

VXID in VSL queries
~~~~~~~~~~~~~~~~~~~

The Varnish Shared Log (VSL) became much more powerful starting Varnish
4.0 and hasn't changed much since. Changes usually consist in adding new
log records when new feature are introduced, or when we realize that some
missing piece of information could really help troubleshooting.

Varnish UTilities (VUT) relying on the VSL usually share the same ``-q``
option for querying, which allows to filter transactions based on log
records. For example you could be looking for figures on a specific
domain::

    varnishtop -i ReqURL -q 'ReqHeader:Host eq www.example.com'

While options like ``-i`` and ``-q`` were until now both limited to log
records, it also meant you could only query a specific transaction using
the ``X-Varnish`` header. Depending on the nature of the transaction
(client or backend side) the syntax is not the same and you can't match
a session.

For instance, we are looking for the transaction 1234 that occurred very
recently and we would like to collect everything from the same session.
We have two options::

    # client side
    varnishlog -d -g session -q 'RespHeader:X-Varnish[1] == 1234'

    # backend side
    varnishlog -d -g session -q 'BereqHeader:X-Varnish == 1234'

There was no simple way to match any transaction using its id until the
introduction of ``vxid`` as a possible left-hand side of a ``-q`` query
expression::

    # client side
    varnishlog -d -g session -q 'vxid == 1234'

    # backend side
    varnishlog -d -g session -q 'vxid == 1234'

    # session
    varnishlog -d -g session -q 'vxid == 1234'

Another use case is the collection of non-transactional logs. With raw
grouping the output is organized differently and each record starts with
its transaction id or zero for non-transactional logs::

    # before 5.1
    varnishlog -g raw | awk '$1 == 0'

    # from now on
    varnishlog -g raw -q 'vxid == 0'

This should offer you a more concise, and more consistent means to filter
transactions with ``varnishlog`` and other VUTs.

.. _whatsnew_changes_5.1_vtest:

Project tool improvements
~~~~~~~~~~~~~~~~~~~~~~~~~

We have spent a fair amount of time on the tools we use internally
in the project.

The ``varnishtest`` program has been improved in many small ways,
in particular it is now much easier to execute and examine
results from other programs with the ``shell`` and ``process``
commands. It might break existing test cases if you were already
using ``varnishtest``.

The project now has *KISS* web-backend which summarizes
``make distcheck`` results from various platforms:

http://varnish-cache.org/vtest/

If you want Varnish to be tested on a platform not already
covered, all you need to do is run the tools/vtest.sh script
from the source tree.  We would love to see more platforms
covered (arm64, ppc, mips) and OS/X would also be nice.

We also publish our code-coverage status now:

http://varnish-cache.org/gcov/

Our goal is 90+% coverage, but we need to start implementing
terminal emulation in ``varnishtest`` before we can test the curses(1)
based programs (top/stat/hist) comprehensively, so they currently
drag us down.


News for authors of VMODs and Varnish API client applications
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* The VRT version has been bumped to 6.0, since there have been some
  changes and additions to the ABI. See ``vrt.h`` for an overview.

* In particular, there have been some changes to the ``WS_*``
  interface for accessing workspaces. We are working towards fully
  encapsulating workspaces with the ``WS_*`` family of functions, so
  that it should not be necessary to access the internals of a
  ``struct ws``, which may be revised in a future release. There are
  no revisions at present, so your code won't break if you're
  working with the innards of a ``struct ws`` now, but you would be
  prudent to replace that code with ``WS_*`` calls some time before
  the next release. And please let us know if there's something you
  need to do that the workspace interface won't handle.

* ``libvarnishapi.so`` now exports more symbols from Varnish internal
  libraries:

  * All of the ``VTIM_*`` functions -- getting clock times, formatting
    and parsing date & time formats, sleeping and so forth.

  * All of the ``VSB_*`` functions for working with safe string
    buffers.

* ``varnish.m4`` and ``varnishapi.pc`` now expose more information about
  the Varnish installation. See "Since 5.1.0" comments for a comprehensive
  list of what was added.

* VMOD version coexistence improvements:  In difference from executable
  files, shared libraries are not protected against overwriting under
  UNIX, and this has generally caused grief when VMODs were updated
  by package management tools.

  We have decided to bite the bullet, and now the Varnishd management
  process makes a copy of the VMOD shared library to a version-unique
  name inside the workdir, from which the running VCL access it.  This
  ensures that Varnishd can always restart the worker process, no matter
  what happened to the original VMOD file.

  It also means that VMODs maintaining state spanning VCL reloads might
  break. It is still possible to maintain global state in a VMOD despite
  VMOD caching: one solution is to move the global state into separate
  shared library that won't be cached by Varnish.

*EOF*
