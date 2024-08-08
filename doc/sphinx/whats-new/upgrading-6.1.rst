..
	Copyright (c) 2018-2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _whatsnew_upgrading_6.1:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 6.1
%%%%%%%%%%%%%%%%%%%%%%%%

A configuration for Varnish 6.0.x will run for version 6.1 without
changes.  There has been a subtle change in the interpretation of the
VCL variable ``beresp.keep`` under specific circumstances, as
discussed below. Other than that, the changes in 6.1 are new features,
described in the following.

varnishd parameters
===================

We have added the :ref:`ref_param_max_vcl` parameter to set a
threshold for the number of loaded VCL programs, since it is a common
error to let previous VCL instances accumulate without discarding
them. The remnants of undiscarded VCLs take the form of files in the
working directory of the management process. Over time, too many of
these may take up significant storage space, and administrative
operations such as ``vcl.list`` may become noticeably slow, or even
time out, when Varnish has to iterate over many files.

The default threshold in :ref:`ref_param_max_vcl` is 100, and VCL
labels are not counted against the total. The
:ref:`ref_param_max_vcl_handling` parameter controls what happens when
you reach the limit. By default you just get a warning from the VCL
compiler, but you can set it to refuse to load more VCLs, or to ignore
the threshold.

Added the :ref:`ref_param_backend_local_error_holddown` and
:ref:`ref_param_backend_remote_error_holddown` parameters. These define
delays for new attempts to connect to backends when certain classes of
errors have been encountered, for which immediate re-connect attempts
are likely to be counter-productive. See the parameter documentation
for details.

Changes to VCL
==============

VCL variables
~~~~~~~~~~~~~

``req.ttl``, ``req.grace`` and keep
-----------------------------------

``req.grace`` had been previously removed, but was now reintroduced,
since there are use cases that cannot be solved without it. Similarly,
``req.ttl`` used to be deprecated and is now fully supported again.

``req.ttl`` and ``req.grace`` limit the ttl and grace times that are
permitted for the current request. If ``req.ttl`` is set, then cache
objects are considered fresh (and may be cache hits) only if their
remaining ttl is less than or equal to ``req.ttl``. Likewise,
``req.grace`` sets an upper bound on the time an object has spent in
grace to be considered eligible for grace mode (which is to deliver
this object and fetch a fresh copy in the background).

A common application is to set shorter TTLs when the backend is known
to be healthy, so that responses are fresher when all is well. But if
the backend is unhealthy, then use cached responses with longer TTLs
to relieve load on the troubled backend::

  sub vcl_recv {
	# ...
	if (std.healthy(req.backend_hint)) {
		# Get responses no older than 70s for healthy backends
		set req.ttl = 60s;
		set req.grace = 10s;
	}

	# If the backend is unhealthy, then permit cached responses
	# that are older than 70s.
  }

The evaluation of the ``beresp.keep`` timer has changed a
bit. ``keep`` sets a lifetime in the cache in addition to TTL for
objects that can be validated by a 304 "Not Modified" response from
the backend to a conditional request (with ``If-None-Match`` or
``If-Modified-Since``). If an expired object is also out of grace
time, then ``vcl_hit`` will no longer be called, so it is impossible
to deliver the "keep" object in this case.

Note that the headers ``If-None-Match`` and ``If-Modified-Since``,
together with the 304 behavior, are handled automatically by Varnish.
If you, for some reason, need to explicitly disable this for a backend
request, then you need do this by removing the headers in
``vcl_backend_fetch``.

The documentation in :ref:`users-guide-handling_misbehaving_servers`
has been expanded to discuss these matters in greater depth, look
there for more details.

``beresp.filters`` and support for backend response processing with VMODs
-------------------------------------------------------------------------

The ``beresp.filters`` variable is readable and writable in
``vcl_backend_response``. This is a space-separated list of modules
that we call VFPs, for "Varnish fetch processors", that may be applied
to a backend response body as it is being fetched. In default Varnish,
the list may include values such as ``gzip``, ``gunzip``, and ``esi``,
depending on how you have set the ``beresp.do_*`` variables.

This addition makes it possible for VMODs to define VFPs to filter or
manipulate backend response bodies, which can be added by changing the
list in ``beresp.filters``. VFPs are applied in the order given in
``beresp.filters``, and you may have to ensure that a VFP is
positioned correctly in the list, for example if it can only apply to
uncompressed response bodies.

This is a new capability, and at the time of release we only know of
test VFPs implemented in VMODs. Over time we hope that an "ecology" of
VFP code will develop that will enrich the features available to
Varnish deployments.

``obj.hits``
------------

Has been fixed to return the correct value in ``vcl_hit`` (it had been
0 in ``vcl_hit``).

Other changes to VCL
~~~~~~~~~~~~~~~~~~~~

* The ``Host`` header in client requests is mandatory for HTTP/1.1, as
  proscribed by the HTTP standard. If it is missing, then
  ``builtin.vcl`` causes a synthetic 400 "Bad request" response to be
  returned.

* You can now provide a string argument to ``return(fail("Foo!"))``,
  which can be used in ``vcl_init`` to emit an error message if the
  VCL load fails due to the return.

* Additional ``import`` statements of an already imported vmod are now
  ignored.

VMODs
=====

Added the :ref:`std.fnmatch()` function to :ref:`vmod_std(3)`, which
you can use for shell-style wildcard matching. Wildcard patterns may
be a good fit for matching URLs, to match against a pattern like
``/foo/*/bar/*``. The patterns can be built at runtime, if you need to
do that, since they don't need the pre-compile step at VCL load time
that is required for regular expressions. And if you are simply more
comfortable with the wildcard syntax than with regular expressions,
you now have the option.

:ref:`vmod_unix(3)` is now supported for SunOS and descendants. This
entails changing the privilege set of the child process while the VMOD
is loaded, see the documentation.

Other changes
=============

* ``varnishd(1)``:

  * Some VCL compile-time error messages have been improved, for
    example when a symbol is not found or arguments to VMOD calls are
    missing.

  * Varnish now won't rewrite the ``Content-Length`` header when
    responding to any HEAD request, making it possible to cache
    responses to HEAD requests independently from the GET responses
    (previously a HEAD request had to be a pass to avoid this
    rewriting).

  * If you have set ``.proxy_header=1`` (to use the PROXYv1 protocol)
    for a backend addressed as a Unix domain socket (with a ``.path``
    setting for the socket file), and have also defined a probe for
    the backend, then the address family ``UNKNOWN`` is sent in
    the proxy header for the probe request. If you have set
    ``.proxy_header=2`` (for PROXYv2) for a UDS backend with a probe,
    then ``PROXY LOCAL`` is sent for the probe request.

* ``varnishlog(1)`` and ``vsl(7)``:

  * The contents of ``FetchError`` log entries have been improved to
    give better human-readable diagnostics for certain classes of
    backend fetch failures.

    In particular, http connection (HTC) errors are now reported
    symbolically in addition to the previous numerical value.

  * Log entries under the new ``SessError`` tag now give more
    diagnostic information about session accept failures (failure to
    accept a client connection). These must be viewed in raw grouping,
    since accept failures are not part of any request/response
    transaction.

  * When a backend is unhealthy, ``Backend_health`` now reports some
    diagnostic information in addition to the HTTP response and timing
    information.

  * The backend name logged for ``Backend_health`` is just the backend
    name without the VCL prefix (as appears otherwise for backend
    naming).

  * Added the log entry tag ``Filters``, which gives a list of the
    filters applied to a response body (see ``beresp.filters``
    discussed above).

* ``varnishadm(1)`` and ``varnish-cli(7)``

  * For a number of CLI commands, you can now use the ``-j`` argument
    to get a JSON response, which may help in automation. These include:

    * ``ping -j``

    * ``backend.list -j``

    * ``help -j``

    A JSON response in the CLI always includes a timestamp (epoch time
    in seconds with millisecond precision), indicating the time at
    which the response was generated.

  * The ``backend.list`` command now lists both directors and
    backends, with their health status. The command now has a ``-v``
    option for verbose output, in which detailed health states for
    each backend/director are displayed.

* ``varnishstat(1)`` and ``varnish-counters(7)``:

  * We have added a number of counters to the ``VBE.*`` group to help
    better diagnose error conditions with backends:

    * ``VBE.*.unhealthy``: the number of fetches that were not
      attempted because the backend was unhealthy

    * ``.busy``: number of fetches that were not attempted because the
      ``.max_connections`` limit was reached

    * ``.fail``: number of failed attempts to open a connection to the
      backend. Detailed reasons for the failures are given in the
      ``.fail_*`` counters (shown at DIAG level), and in the log entry
      ``FetchError``. ``.fail`` is the sum of the values in the
      ``.fail_*`` counters.

    * ``.fail_eaccess``, ``.fail_eaddrnotavail``,
      ``.fail_econnrefused``, ``.fail_enetunreach`` and
      ``.fail_etimedout``: these are the number of attempted
      connections to the backend that failed with the given value of
      ``errno(3)``.

    * ``.fail_other``: number of connections to the backend that
      failed for reasons other than those given by the other
      ``.fail_*`` counters. For such cases, details on the failure
      can be extracted from the varnish log as described above for
      ``FetchError``.

    * ``.helddown``: the number of connections not attempted because
      the backend was in the period set by one of the parameters
      :ref:`ref_param_backend_local_error_holddown` or
      :ref:`ref_param_backend_remote_error_holddown`

  * Similarly, we have added a series of counters for better diagnostics
    of session accept failures (failure to accept a connection from a
    client). As before, the ``sess_fail`` counter gives the total number
    of accept failures, and it is now augmented with the ``sess_fail_*``
    counters. ``sess_fail`` is the sum of the values in ``sess_fail_*``.

    * ``sess_fail_econnaborted``, ``sess_fail_eintr``,
      ``sess_fail_emfile``, ``sess_fail_ebadf`` and
      ``sess_fail_enomem``: the number of accept failures with the
      indicated value of ``errno(3)``. The :ref:`varnish-counters(7)`
      man page, and the "long descriptions" shown by ``varnishstat``,
      give possible reasons why each of these may happen, and what
      might be done to counter the problem.

    * ``sess_fail_other``: number of accept failures for reasons
      other than those given by the other ``sess_fail_*`` counters.
      More details may appear in the ``SessError`` entry of the log
      (:ref:`varnish-counters(7)` shows a ``varnishlog`` invocation
      that may help).

  * In curses mode, the information in the header lines (uptimes and
    cache hit rates) is always reported, even if you have defined a
    filter that leaves them out of the stats table.

  * Ban statistics are now reported more accurately (they had been
    subject to inconsistencies due to race conditions).

* ``varnishtest(1)`` and ``vtc(7)``:

  * ``varnishtest`` and the ``vtc`` test script language now support
    testing for haproxy as well as Varnish. The ``haproxy`` directive
    in a test can be used to define, configure, start and stop a
    haproxy instance, and you can also script messages to send on the
    haproxy CLI connection, and define expectations for the
    responses. See the ``haproxy`` section in :ref:`vtc(7)` for
    details.

  * Related to haproxy support, you can now define a ``syslog``
    instance in test scripts. This defines a syslog server, and allows
    you to test expectations for syslog output from a haproxy
    instance.

  * Added the ``-keepalive`` argument for client and server scripts to
    be used with the ``-repeat`` directive, which causes all test
    iterations to run on the same connection, rather than open a new
    connection each time. This makes the test run faster and use fewer
    ephemeral ports.

  * Added the ``-need-bytes`` argument for the ``process`` command,
    see :ref:`vtc(7)`.

* ``varnishhist(1)``:

  * The ``-P min:max`` command-line parameters are now optional,
    see :ref:`varnishhist(1)`.

* For all of the utilities that access the Varnish log --
  ``varnishlog(1)``, ``varnishncsa(1)``, ``varnishtop(1)`` and
  ``varnishhist(1)`` -- it was already possible to set multiple ``-I``
  and ``-X`` command-line arguments.  It is now properly documented
  that you can use multiple include and exclude filters that apply
  regular expressions to selected log records.

* Changes for developers:

  * As mentioned above, VMODs can now implement VFPs that can be added
    to backend response processing by changing ``beresp.filters``.
    The interface for VFPs is defined in ``cache_filters.h``, and the
    debug VMOD included in the distribution shows an example of a
    VFP for rot13.

  * The Varnish API soname version (for libvarnishapi.so) has been
    bumped to 2.0.0.

  * The VRT version has been bumped to 8.0. See ``vrt.h`` for details
    on the changes since 7.0.

  * Space required by varnish for maintaining the ``PRIV_TASK`` and
    ``PRIV_TOP`` parameters is now taken from the appropriate
    workspace rather than from the heap as before. For a failing
    allocation, a VCL failure is triggered.

    The net effect of this change is that in cases of a workspace
    shortage, the almost unavoidable failure will happen earlier. The
    amount of workspace required is slightly increased and scales with
    the number of vmods per ``PRIV_TASK`` and ``PRIV_TOP`` parameter.

    The VCL compiler (VCC) guarantees that if a vmod function is
    called with a ``PRIV_*`` argument, that argument value is set.

    There is no change with respect to the API the ``PRIV_*`` vmod
    function arguments provide.

  * ``VRT_priv_task()``, the function implementing the allocation of
    the ``PRIV_TASK`` and ``PRIV_TOP`` parameters as described above,
    is now more likely to return ``NULL`` for allocation failures for
    the same reason.

    Notice that explicit use of this function from within VMODs is
    considered experimental as this interface may change.

  * We have improved support for the ``STRANDS`` data type, which you
    may find easier to use than the varargs-based ``STRING_LIST``. See
    ``vrt.h`` for details.  :ref:`vmod_blob(3)` has been refactored to
    use ``STRANDS``, so you can look there for an example.

  * We have fixed a bug that had limited the precision available for
    the ``INT`` data type, so you now get the full 64 bits.

  * Portions of what had previously been declared in
    ``cache_director.h`` have been moved into ``vrt.h``, constituting
    the public API for directors. The remainder in
    ``cache_director.h`` is not public, and should not be used by a
    VMOD intended for VRT ABI compatibility.

  * The director API in ``vrt.h`` differs from the previous
    interface. :ref:`ref-writing-a-director` has been updated
    accordingly. In short, the most important changes include:

    * ``struct director_methods`` is replaced by ``struct vdi_methods``
    * signatures of various callbacks have changed
    * ``VRT_AddDirector()`` and ``VRT_DelDirector()`` are to be used
      for initialization and destruction.
    * ``vdi_methods`` callbacks are not to be called from vmods any more
    * ``VRT_Healthy()`` replaces calls to the ``healthy`` function
    * The admin health is not to be manipulated by vmods any more
    * director private state destruction is recommended to be
      implemented via a ``destroy`` callback.

  * Python 3 is now preferred in builds, and will likely be required
    in future versions.

  * We believe builds are now reproducible, and intend to keep them
    that way.

*eof*
