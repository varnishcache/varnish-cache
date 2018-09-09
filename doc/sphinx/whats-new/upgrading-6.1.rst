.. _whatsnew_upgrading_6.1:

**NOTE: The present document is work in progress for the September
2018 release.** The version number 6.1.0 is provisional and may
change. See :ref:`whatsnew_upgrading_6.0` for notes about the
currently most recent Varnish release.

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 6.1
%%%%%%%%%%%%%%%%%%%%%%%%

.. _upd_6_1_headline:

**Headline Changes**
====================

**XXX**

varnishd parameters
===================

We have added the :ref:`ref_param_max_vcl` parameter to set a
threshold for the number of loaded VCL programs, since it is a common
error to let previous VCL instances accumulate without discarding
them. The default threshold is 100, and VCL labels are not counted
against the total. The :ref:`ref_param_max_vcl_handling` parameter
controls what happens when you reach the limit. By default you just
get a warning from the VCL compiler, but you can set it to refuse to
load more VCLs, or to ignore the threshold.

Added the :ref:`ref_param_backend_local_error_holddown` and
:ref:`ref_param_backend_remote_error_holddown` parameters. These define
delays for new attempts to connect to backends when certain classes of
errors have been encountered, for which immediate re-connect attempts
are likely to be counter-productive. See the parameter documentation
for details.

Changes to VCL
==============

**Headline VCL changes**
~~~~~~~~~~~~~~~~~~~~~~~~

**XXX**

VCL variables
~~~~~~~~~~~~~

``req.ttl``, ``req.grace`` and keep
-----------------------------------

``req.ttl`` had been previously listed as deprecated, but it is now
fully supported, since there are use cases that cannot be solved
without it.

``req.ttl`` and ``req.grace`` set upper bounds on the TTL and grace
times that are permitted for the current request -- if these variables
are set and the TTL/grace of a cache object is longer than their
settings, then a new response is fetched from the backend, despite the
presence of the response in the cache.

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
	# ...
  }

The evaluation of the ``beresp.keep`` timer has changed a
bit. ``keep`` sets a lifetime in the cache in addition to TTL for
objects that can be validated by a 304 "Not Modified" response from
the backend to a conditional request (with ``If-None-Match`` or
``If-Modified-Since``). If an expired object is also out of grace
time, it is no longer possible to deliver a "keep" object from
``vcl_hit``. It is possible to validate a 304 candidate from
``vcl_miss``.

The documentation in :ref:`users-guide-handling_misbehaving_servers`
has been expanded to discuss these matters in greater depth, look
there for more details.

``var2``
--------

**XXX**

Other changes
~~~~~~~~~~~~~

You can now provide a string argument to ``return(fail("Foo!"))``,
which can be used in ``vcl_init`` to emit an error message if the VCL
load fails due to the return.

If you have set ``.proxy_header=1`` (to use the PROXYv1 protocol) for
a backend addressed as a Unix domain socket (with a ``.path`` setting
for the socket file), and have also defined a probe for the backend,
then then the address family ``UNKNOWN`` is sent in the proxy header
for the probe request. If you have set ``.proxy_header=2`` (for
PROXYv2) for a UDS backend with a probe, then ``PROXY LOCAL`` is sent
for the probe request.

VMODs
=====

Added the :ref:`func_fnmatch` function to :ref:`vmod_std(3)`, which
you can use for shell-style wildcard matching (if you prefer that to
regular expressions).

:ref:`vmod_unix(3)` is now supported for SunOS and descendants. This
entails changing the privilege set of the child process while the VMOD
is loaded, see the documentation.

**anything else**
=================

**XXX**

Other changes
=============

* ``varnishd(1)``:

  * Some VCL compile-time error messages have been improved, for
    example when a symbol is not found or arguments to VMOD calls are
    missing.

  * **XXX**

* ``varnishlog(1)``:

  * When a backend is unhealthy, ``Backend_health`` now reports some
    diagnostic information in addition to the HTTP response and timing
    information.

  * The backend name logged for ``Backend_health`` is just the backend
    name without the VCL prefix (as appears otherwise for backend
    naming).

  * **XXX**

* ``varnishadm(1)`` and ``varnish-cli(7)``

  * For a number of CLI commands, you can now use the ``-j`` argument
    to get a JSON response, which may help in automation. These include:

    * ``ping -j``

    * **XXX...**

    A JSON response in the CLI always includes a timestamp (epoch time in
    seconds with millisecond precision).

* ``varnishstat(1)`` and ``varnish-counters(7)``:

  * We have added a number of counters to the ``VBE.*`` group to help
    better diagnose error conditions with backends:

    * ``VBE.*.unhealthy``: the number of fetches that were not
      attempted because the backend was unhealthy

    * ``.busy`` number of fetches that were not attempted because the
      ``.max_connections`` limit was reached

    * ``.fail``: number of failed attempts to open a connection to the
      backend. Detailed reasons for the failures are given in the
      ``.fail_*`` counters (shown at DIAG level), and in the log entry
      ``Debug``. ``.fail`` is the sum of the values in the ``.fail_*``
      counters.

    * ``.fail_eaccess``, ``.fail_eaddrnotavail``,
      ``.fail_econnrefused``, ``.fail_enetunreach`` and
      ``.fail_etimedout``: these are the number of attempted
      connections to the backend that failed with the given value of
      ``errno(3)``.

    * ``.fail_other``: number of connections to the backend that
      failed for reasons other than those given by the other
      ``.fail_*`` counters.

    * ``.helddown``: the number of connections not attempted because
      the backend was in the period set by one of the parameters
      :ref:`ref_param_backend_local_error_holddown` or
      :ref:`ref_param_backend_remote_error_holddown`

  * In curses mode, the information in the header lines (uptimes and
    cache hit rates) is always reported, even if you have defined a
    filter that leaves them out of the stats table.

  * Ban statistics are now reported more accurately (they had been
    subject to inconsistencies due to race conditions).

* ``varnishtest(1)`` and ``vtc(7)``:

  * ``varnishtest`` and the ``vtc`` test script language now supports
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

  * **XXX**

* Changes for developers:

  * The Varnish API soname version (for libvarnishapi.so) has been
    bumped to 2.0.0.

  * We have improved support for the ``STRANDS`` data type, which you
    may find easier to use than the varargs-based ``STRING_LIST``. See
    ``vrt.h`` for details.  :ref:`vmod_blob(3)` has been refactored to
    use ``STRANDS``, so you can look there for an example.

  * We have fixed a bug that had limited the precision available for
    the ``INT`` data type, so you now get the full 64 bits.

  * Python 3 is now preferred in builds, and will likely be required
    in future versions.

*eof*
