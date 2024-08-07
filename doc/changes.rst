..
	Copyright (c) 2011-2023 Varnish Software AS
	Copyright 2016-2023 UPLEX - Nils Goroll Systemoptimierung
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. role:: ref(emphasis)

===================
About this document
===================

.. keep this section at the top!

This document contains notes from the Varnish developers about ongoing
development and past versions:

* Developers will note here changes which they consider particularly
  relevant or otherwise noteworthy

* This document is not necessarily up-to-date with the code

* It serves as a basis for release managers and others involved in
  release documentation

* It is not rendered as part of the official documentation and thus
  only available in ReStructuredText (rst) format in the source
  repository and -distribution.

Official information about changes in releases and advise on the
upgrade process can be found in the ``doc/sphinx/whats-new/``
directory, also available in HTML format at
http://varnish-cache.org/docs/trunk/whats-new/index.html and via
individual releases. These documents are updated as part of the
release process.

===============================
Varnish Cache NEXT (2024-09-15)
===============================

.. PLEASE keep this roughly in commit order as shown by git-log / tig
   (new to old)

* Backend tasks can now queue if the backend has reached its max_connections.
  This allows the task to wait for a connection to become available rather
  than immediately failing. This feature must be enabled with the new
  parameters added.

  New parameters:
  ``backend_wait_timeout`` sets the amount of time a task will wait.
  ``backend_wait_limit`` sets the maximum number of tasks that can wait.

  These parameters can also be set in the backend with ``wait_timeout``
  and ``wait_limit``.

  New counters:
  ``backend_wait`` count of tasks that waited in queue for a connection.
  ``backend_wait_fail`` count of tasks that waited in queue but did not get
  a connection within the ``wait_timeout``.

* The ObjWaitExtend() Object API function gained a ``statep`` argument
  to optionally return the busy object state consistent with the
  current extension. A ``NULL`` value may be passed if the caller does
  not require it.

* for backends using the ``.via`` attribute to connect through a
  *proxy*, the ``connect_timeout``, ``first_byte_timeout`` and
  ``between_bytes_timeout`` attributes are now inherited from *proxy*
  unless explicitly given.

* ``varnishd`` now creates a ``worker_tmpdir`` which can be used by
  VMODs for temporary files. The `VMOD developer documentation`_ has
  details.

* The environment variable ``VARNISH_DEFAULT_N`` now provides the
  default "varnish name" / "workdir" as otherwise specified by he
  ``-n`` argument to ``varnishd`` and ``varnish*`` utilities except
  ``varnishtest``.

.. _VMOD developer documentation: doc/sphinx/reference/vmod.rst

================================
Varnish Cache 7.5.0 (2024-03-18)
================================

* Add ``h2_window_timeout`` paramater to mitigate CVE-2023-43622 (VSV00014_).

* The parameters ``idle_send_timeout`` and ``timeout_idle`` are now
  limited to a maximum of 1 hour.

* The VCL variables ``bereq.connect_timeout``,
  ``bereq.first_byte_timeout``, ``bereq.between_bytes_timeout``,
  ``bereq.task_deadline``, ``sess.timeout_idle``,
  ``sess.timeout_linger``, ``sess.idle_send_timeout`` and
  ``sess.send_timeout`` can now be ``unset`` to use their default
  values from parameters.

* Timeout and deadline parameters can now be set to a new special value
  ``never`` to apply an infinitely long timeout. Parameters which used to
  be of type ``timeout`` but do not accept ``never`` have been moved to
  the new type ``duration``. VCL variables cannot be set to ``never``.

* The implementation of the feature flag ``esi_include_onerror`` changed
  in Varnish-Cache 7.3.0 has been reverted to more closely match the
  behavior before that release: By default, fragments are included
  again, even errors. When ``esi_include_onerror`` is enabled and
  errors are encountered while processing an ESI fragment, processing
  only continues if the ``onerror`` attribute of the ``<esi:include>``
  tag is present.

  Any response status other than ``200`` or ``204`` counts as an error
  as well as any fetch error.

  Streaming responses may continue to be partially delivered.

  Error behavior has been fixed to be consistent also for zero length
  fragments.

* The new VSC ``n_superseded`` gets incremented every time an object
  is superseded by a new one, for example when the grace and/or keep
  timers kept it in cache for longer than the TTL and a fresh copy is
  fetched.

  Cache evictions of superseded objects are logged as ``ExpKill``
  messages starting with ``VBF_Superseded``.

  .. _Varnish-Modules #222: https://github.com/varnish/varnish-modules/issues/222

* The implementation of ``PRIV_TASK`` and ``PRIV_TOP`` VMOD
  function/method arguments has been fixed to also work with
  ``std.rollback()`` (`Varnish-Modules #222`_)

* Transports are now responsible for calling ``VDP_Close()`` in all
  cases.

* The format of ``BackendClose`` VSL records has been changed to use
  the short reason name for consistency with  ``SessClose``.

* During ``varnishd`` shutdown, pooled backend connections are now
  closed bi-directionally.

* Mode bits of files opened via the UNIX jail as ``JAIL_FIXFD_FILE``
  are now correctly set as ``0600``.

* The ``busy_stats_rate`` feature now also works for HTTP/2.

* The ``BUILD_VMOD_$NAME`` m4 macro for VMOD Makefiles has been fixed
  to properly support custom ``CFLAGS``.

* Storage engines are now responsible for deciding which
  ``fetch_chunksize`` to use. When Varnish-Cache does not know the
  expected object size, it calls the ``objgetspace`` stevedore
  function with a zero ``sz`` argument.

* The ``Timestamp`` SLT with ``Process`` prefix is not emitted any
  more when processing continues as for restarts, or when ``vcl_deliver``
  transitions to ``vcl_synth``.

* The ``FetchError`` SLT with ``HTC`` prefix now contains a verbose
  explanation.

* Varnish Test Cases (VTCs) now support an ``include`` statement.

* ``varnishncsa`` now supports the ``%{Varnish:default_format}x``
  format to use the default format with additions.

* A deadlock in ``VRT_AddDirector()`` is now avoided with dynamic
  backends when the VCL goes cold.

* A new variable ``bereq.task_deadline``, available in ``sub vcl_pipe
  {}`` only for now, allows to limit the total duration of pipe
  transactions. Its default comes from the ``pipe_task_deadline``
  parameter, which itself defaults to ``never``.

* The VSC counters ``n_expired``, ``n_purges`` and ``n_obj_purged``
  have been fixed for purged objects.

* The ``ExpKill`` SLT prefix ``EXP_expire`` has been renamed to
  ``EXP_Inspect``.

* New VSL records of the ``ExpKill`` SLT with ``EXP_Removed`` are now
  emitted to uniformly log all "object removed from cache" events.

* VSL records of the ``ExpKill`` SLT with ``EXP_Expired`` prefix now
  contain the number of hits on the removed object.

* A bug has been fixed in ``varnishstat`` where the description of the
  last VSC was not shown.

* VCL COLD events have been fixed for directors vs. VMODs: VDI COLD
  now comes before VMOD COLD.

* The ``file`` storage engine now fails properly if the file size is
  too small.

* The ``.happy`` stevedore type method now returns ``true`` if not
  implemented instead of panicking ``varnishd`` (`4036`_)

* Use of ``objiterate_f`` on request bodies has been fixed to
  correctly post ``OBJ_ITER_END``.

* Use of ``STV_NewObject()`` has been fixed to correctly request zero
  bytes for attributes where only a body is to be stored.

* ``(struct req).filter_list`` has been renamed to ``vdp_filter_list``.

* 304 object copying has been optimized to make optimal use of storage
  engines' allocations.

* Use of the ``trimstore`` storage engine function has been fixed for
  304 responses.

* A missing ``:scheme`` for HTTP/2 requests is now properly handled.

* The ``fold`` flag has been added to Access Control Lists (ACLs)
  in VCL. When it is activated with ``acl ... +fold {}``, ACL entries
  get optimized in that subnets contained in other entries are skipped
  (e.g.  if 1.2.3.0/24 is part of the ACL, an entry for 1.2.3.128/25
  will not be added) and adjacent entries get folded (e.g.  if both
  1.2.3.0/25 and 1.2.3.128/25 are added, they will be folded to
  1.2.3.0/24) (3563_).

  Logging under the ``VCL_acl`` tag can change with this flag.

  Negated ACL entries are never folded.

* Fixed handling of failing sub-requests: A VCL failure on the client
  side or the ``vcl_req_reset`` feature could trigger a panic, because
  it is not allowed to generate a minimal response. For sub-requests,
  we now masquerade the fail transition as a deliver and trade the
  illegal minimal response for the synthetic response (4022_).

* The ``param.reset [-j]`` CLI command has been added to reset flags
  to their default. Consequently, the ``param.set ... default``
  special value is now deprecated.

* The ``param.set`` CLI command now supports the ``none`` and ``all``
  values to achieve setting "absolute" values atomically as in
  ``param.set foo none,+bar,+baz`` or ``param.set foo all,-bar,-baz``.

* A glitch in CLI command parsing has been fixed where individually
  quoted arguments like ``"help"`` were rejected.

* The ``vcl_req_reset`` feature (controllable through the ``feature``
  parameter, see `varnishd(1)`) has been added and enabled by default
  to terminate client side VCL processing early when the client is
  gone.

  *req_reset* events trigger a VCL failure and are reported to
  `vsl(7)` as ``Timestamp: Reset`` and accounted to ``main.req_reset``
  in `vsc` as visible through ``varnishstat(1)``.

  In particular, this feature is used to reduce resource consumption
  of HTTP/2 "rapid reset" attacks (see below).

  Note that *req_reset* events may lead to client tasks for which no
  VCL is called ever. Presumably, this is thus the first time that
  valid `vcl(7)` client transactions may not contain any ``VCL_call``
  records.

* Added mitigation options and visibility for HTTP/2 "rapid reset"
  attacks (CVE-2023-44487_, 3996_, 3997_, 3998_, 3999_).

  Global rate limit controls have been added as parameters, which can
  be overridden per HTTP/2 session from VCL using the new vmod ``h2``:

  * The ``h2_rapid_reset`` parameter and ``h2.rapid_reset()`` function
    define a threshold duration for an ``RST_STREAM`` to be classified
    as "rapid": If an ``RST_STREAM`` frame is parsed sooner than this
    duration after a ``HEADERS`` frame, it is accounted against the
    rate limit described below.

    The default is one second.

  * The ``h2_rapid_reset_limit`` parameter and
    ``h2.rapid_reset_limit()`` function define how many "rapid" resets
    may be received during the time span defined by the
    ``h2_rapid_reset_period`` parameter / ``h2.rapid_reset_period()``
    function before the HTTP/2 connection is forcibly closed with a
    ``GOAWAY`` and all ongoing VCL client tasks of the connection are
    aborted.

    The defaults are 100 and 60 seconds, corresponding to an allowance
    of 100 "rapid" resets per minute.

  * The ``h2.rapid_reset_budget()`` function can be used to query the
    number of currently allowed "rapid" resets.

  * Sessions closed due to rapid reset rate limiting are reported as
    ``SessClose RAPID_RESET`` in `vsl(7)` and accounted to
    ``main.sc_rapid_reset`` in `vsc` as visible through
    ``varnishstat(1)``.

* The ``cli_limit`` parameter default has been increased from 48KB to
  64KB.

* ``VSUB_closefrom()`` now falls back to the base implementation not
  only if ``close_range()`` was determined to be unusable at compile
  time, but also at run time. That is to say, even if
  ``close_range()`` is compiled in, the fallback to the naive
  implementation remains.

* Fixed ``varnishd -I`` error reporting when a final newline or
  carriage return is missing in the CLI command file (3995_).

* Improved and updated the build system with respect to autoconf and
  automake.

* Improved ``VSB_tofile()`` error reporting, added support for partial
  writes and support of VSBs larger than INT_MAX.

* Improved HPACK header validation.

* Fixed scopes of protected headers (3984_).

.. _CVE-2023-44487: https://nvd.nist.gov/vuln/detail/CVE-2023-44487

.. _4036: https://github.com/varnishcache/varnish-cache/issues/4036
.. _3984: https://github.com/varnishcache/varnish-cache/issues/3984
.. _3995: https://github.com/varnishcache/varnish-cache/issues/3995
.. _3996: https://github.com/varnishcache/varnish-cache/issues/3996
.. _4022: https://github.com/varnishcache/varnish-cache/issues/4022
.. _3563: https://github.com/varnishcache/varnish-cache/pull/3563
.. _3997: https://github.com/varnishcache/varnish-cache/pull/3997
.. _3998: https://github.com/varnishcache/varnish-cache/pull/3998
.. _3999: https://github.com/varnishcache/varnish-cache/pull/3999
.. _VSV00014: https://varnish-cache.org/security/VSV00014.html

================================
Varnish Cache 7.4.0 (2023-09-15)
================================

* The ``VSB_quote_pfx()`` (and, consequently, ``VSB_quote()``) function
  no longer produces ``\v`` for a vertical tab. This improves
  compatibility with JSON.

* The bundled *zlib* has been updated to match *zlib 1.3*.

* The ``VSHA256_*`` functions have been added to libvarnishapi (3946_).

* Tabulation of the ``vcl.list`` CLI output has been modified
  slightly.

* VCL now supports "protected headers", which can neither be set nor unset.

* The ``Content-Length`` and ``Transfer-Encoding`` headers are now
  protected. For the common use case of ``unset
  xxx.http.Content-Length`` to dismiss a body, ``unset xxx.body``
  should be used.

* Error handling of numeric literals in exponent notation has been
  improved in the VCL compiler (3971_).

* Finalization of the storage private state of busy objects has been
  fixed. This bug could trigger a panic when ``vcl_synth {}`` was used
  to replace the object body and storage was changed from one of the
  built-in storage engines to a storage engine from an extension (3953_).

* HTTP/2 header field validation is now more strict with respect to
  allowed characters (3952_).

* A bug has been fixed in the filter handling code which could trigger
  a panic when ``resp.filters`` was used from ``vcl_synth {}`` (3968_).

* The utility macros ``ALLOC_OBJ_EXTRA()`` and ``ALLOC_FLEX_OBJ()``
  have been added to ``miniobj.h`` to simplify allocation of objects
  larger than a struct and such with a flexible array.

* The ``varnishapi`` version has been increased to 3.1 and the
  functions ``VENC_Encode_Base64()`` and ``VENC_Decode_Base64()`` are
  now exposed.

* Two bugs in the ban expression parser have been fixed where one of them
  could lead to a panic if a ban expression with an empty header name was
  issued (3962_).

* The ``v_cold`` macro has been added to add ``__attribute__((cold))``
  on compilers supporting it. It is used for ``VRT_fail()`` to mark
  failure code paths as cold.

* ``varnishtest`` now generates ``User-Agent`` request and ``Server``
  response headers with the respective client and server name by
  default. The ``txreq -nouseragent`` and ``txresp -noserver`` options
  disable addition of these headers.

* Error handling of invalid header names has been improved in the VCL
  Compiler (3960_).

* A race condition has been fixed in the backend probe code which
  could trigger a panic with dynamic backends (dyn100_).

* A bug has been fixed in the ESI code which would prevent use of
  internal status codes >1000 as their modulus 1000 value (3958_).

* The ``varnishd_args_prepend`` and ``varnishd_args_append`` macros
  have been added to ``varnishtest`` to add arguments to ``varnishd``
  invocations before and after the defaults.

* A bug has been fixed where ``varnishd`` would hang indefinitely when
  the worker process would not come up within ``cli_timeout`` (3940_).

* The ``startup_timeout`` parameter now specifically replaces
  ``cli_timeout`` for the initial startup only (3940_).

* On Linux, ``close_range()`` is now used if available (3905_).

* Error reporting has been improved if the working directory
  (``varnishd -n`` argument) resides on a file system mounted
  ``noexec`` (3943_).

* The number of backtrace levels in panic reports has been increased
  from 10 to 20.

* The ``PTOK()`` macro has been added to ``vas.h`` to simplify error
  checking of ``pthread_*`` POSIX functions.

* In ``varnishtest``, the basename of the test directory is now
  available as the ``vtcid`` macro to serve as a unique string across
  concurrently running tests.

* In ``struct vsmwseg`` and ``struct vsm_fantom``, the ``class``
  member has been renamed to ``category``.

* ESI ``onerror=abort`` handling has been fixed when ``max_esi_depth``
  is reached (3938_).

* A spurious *Could not delete 'vcl\_...'* error message has been
  removed (3925_).

* A bug has been fixed where ``unset bereq.body`` had no effect when
  used with a cached body (3914_)

* ``.vcc`` files of VMODs are now installed to
  ``/usr/share/varnish/vcc`` (or equivalent) to enable re-use by other
  tools like code editors.

* The :ref:`vcl-step(7)` manual page has been added to document the
  VCL state machines.

* ``HSH_Cancel()`` has been moved to ``VDP_Close()`` to enable
  transports to keep references to objects.

* VCL tracing now needs to be explicitly activated by setting the
  ``req.trace`` or ``bereq.trace`` VCL variables, which are
  initialized from the ``feature +trace`` flag. Only if the trace
  variables are set will ``VCL_trace`` log records be generated.

  Consequently, ``VCL_trace`` has been removed from the default
  ``vsl_mask``, so any trace records will be emitted by
  default. ``vsl_mask`` can still be used to filter ``VCL_trace``
  records.

  To trace ``vcl_init {}`` and ``vcl_fini {}``, set the ``feature
  +trace`` flag while the vcl is loaded/discarded.

* Varnish Delivery Processors (VDPs) are now also properly closed for
  error conditions, avoiding potential minor memory leaks.

* A regression introduced with Varnish Cache 7.3.0 was fixed: On
  HTTP/2 connections, URLs starting with ``//`` no longer trigger a
  protocol error (3911_).

* Call sites of VMOD functions and methods can now be restricted to
  built-in subroutines using the ``$Restrict`` stanza in the VCC file.

* The counter ``MAIN.http1_iovs_flush`` has been added to track the
  number of premature ``writev()`` calls due to an insufficient number
  of IO vectors. This number is configured through the ``http1_iovs``
  parameter for client connections and implicitly defined by the
  amount of free workspace for backend connections.

* Object creation failures by the selected storage engine are now
  logged under the ``Error`` tag as ``Failed to create object object
  from %s %s``.

* The limit on the size of ``varnishtest`` macros has been raised to
  2KB.

* The newly introduced abstract socket support was incompatible with
  other implementations, this has been fixed (3908_).

.. _3905: https://github.com/varnishcache/varnish-cache/issues/3905
.. _3908: https://github.com/varnishcache/varnish-cache/pull/3908
.. _3911: https://github.com/varnishcache/varnish-cache/issues/3911
.. _3914: https://github.com/varnishcache/varnish-cache/pull/3914
.. _3925: https://github.com/varnishcache/varnish-cache/issues/3925
.. _3938: https://github.com/varnishcache/varnish-cache/issues/3938
.. _3940: https://github.com/varnishcache/varnish-cache/issues/3940
.. _3943: https://github.com/varnishcache/varnish-cache/issues/3943
.. _3946: https://github.com/varnishcache/varnish-cache/issues/3946
.. _3952: https://github.com/varnishcache/varnish-cache/issues/3952
.. _3953: https://github.com/varnishcache/varnish-cache/issues/3953
.. _3958: https://github.com/varnishcache/varnish-cache/issues/3958
.. _3960: https://github.com/varnishcache/varnish-cache/issues/3960
.. _3962: https://github.com/varnishcache/varnish-cache/issues/3962
.. _3968: https://github.com/varnishcache/varnish-cache/issues/3968
.. _3971: https://github.com/varnishcache/varnish-cache/issues/3971

.. _dyn100: https://github.com/nigoroll/libvmod-dynamic/issues/100

================================
Varnish Cache 7.3.0 (2023-03-15)
================================

* The macro ``WS_TASK_ALLOC_OBJ`` as been added to handle the common
  case of allocating mini objects on a workspace.

* ``xid`` variables in VCL are now of type ``INT``.

* The new ``beresp.transit_buffer`` variable has been added to VCL,
  which defaults to the newly added parameter ``transit_buffer``. This
  variable limits the number of bytes varnish pre-fetches for
  uncacheable streaming fetches.

* Varnish now supports abstract unix domain sockets. If the operating
  system supports them, abstract sockets can be specified using the
  commonplace ``@`` notation for accept sockets, e.g.::

    varnishd -a @kandinsky

  and backend paths, e.g.::

    backend miro {
      .path = "@miro";
    }

* For backend requests, the timestamp from the ``Last-Modified``
  response header is now only used to create an ``If-Modified-Since``
  conditional ``GET`` request if it is at least one second older than
  the timestamp from the ``Date`` header.

* Various interfaces of varnish's own socket address abstraction, VSA,
  have been changed to return or take pointers to
  ``const``. ``VSA_free()`` has been added.

* Processing of Range requests has been improved: Previously, varnish
  would send a 200 response with the full body when it could not
  reliably determine (yet) the object size during streaming.

.. `RFC9110`_ : https://httpwg.org/specs/rfc9110.html#field.content-range

  Now a 206 response is sent even in this case (for HTTP/1.1 as
  chunked encoding) with ``*`` in place of the ``complete-length`` as
  per `RFC9110`_.

* The ``debug.xid`` CLI command now sets the next XID to be used,
  rather than "one less than the next XID to be used"

* VXIDs are 64 bit now and the binary format of SHM and raw saved
  VSL files has changed as a consequence.

  The actual valid range for VXIDs is [1…999999999999999], so it
  fits in a VRT_INTEGER.

  At one million cache-missing single request sessions per second
  VXIDs will roll over in a little over ten years::

    (1e15-1) / (3 * 1e6  * 86400 * 365) = 10.57

  That should be enough for everybody™.

  You can test if your downstream log-chewing pipeline handle the
  larger VXIDs correctly using the CLI command::

    ``debug.xid 20000000000``

* Consequently, VSL clients (log processing tools) are now
  incompatible with logs and in-memory data written by previous
  versions, and vice versa.

* Do not ESI:include failed objects unless instructed to.

  Previously, any ESI:include object would be included, no matter
  what the status of it were, 200, 503, didn't matter.

  From now on, by default, only objects with 200 and 204 status
  will be included and any other status code will fail the parent
  ESI request.

  If objects with other status should be delivered, they should
  have their status changed to 200 in VCL, for instance in
  ``sub vcl_backend_error{}``, ``vcl_synth{}`` or ``vcl_deliver{}``.

  If ``param.set feature +esi_include_onerror`` is used, and the
  ``<esi:include …>`` tag has a ``onerror="continue"`` attribute,
  any and all ESI:include objects will be delivered, no matter what
  their status might be, and not even a partial delivery of them
  will fail the parent ESI request.  To be used with great caution.

* Backend implementations are in charge of logging their headers.

* VCL backend ``probe``\ s gained an ``.expect_close`` boolean
  attribute. By setting to to ``false``, backends which fail to honor
  ``Connection: close`` can be probed.

  Notice that the probe ``.timeout`` needs to be reached for a probe
  with ``.expect_close = false`` to return.

* Support for backend connections through a proxy with a PROXY2
  preamble has been added:

  * VCL ``backend``\ s gained attributes ``.via`` and ``.authority``

  * The ``VRT_new_backend_clustered()`` and ``VRT_new_backend()``
    signatures have been changed

* Unused log tags (SLTs) have been removed.

* Directors which take and hold references to other directors via
  ``VRT_Assign_Backend()`` (typically any director which has other
  directors as backends) are now expected to implement the new
  ``.release`` callback of type ``void
  vdi_release_f(VCL_BACKEND)``. This function is called by
  ``VRT_DelDirector()``. The implementation is expected drop any
  backend references which the director holds (again using
  ``VRT_Assign_Backend()`` with ``NULL`` as the second argument).

  Failure to implement this callback can result in deadlocks, in
  particular during VCL discard.

* Handling of the HTTP/2 :path pseudo header has been improved.

================================
Varnish Cache 7.2.0 (2022-09-15)
================================

* Functions ``VRT_AddVDP()``, ``VRT_AddVFP()``, ``VRT_RemoveVDP()`` and
  ``VRT_RemoveVFP()`` are deprecated.

* Cookie headers generated by vmod_cookie no longer have a spurious trailing
  semi-colon (``';'``) at the end of the string. This could break VCL relying
  on the previous incorrect behavior.

* The ``SessClose`` and ``BackendClose`` reason ``rx_body``, which
  previously output ``Failure receiving req.body``, has been rewritten
  to ``Failure receiving body``.

* Prototypical Varnish Extensions (VEXT). Similar to VMODs, a VEXT is loaded
  by the cache process. Unlike VMODs that have the combined lifetime of all
  the VCLs that reference them, a VEXT has the lifetime of the cache process
  itself. There are no built-in extensions so far.

* The VCC (compilation) process no longer loads VMODs with ``dlopen(3)`` to
  collect their metadata.

* Stevedore initialization via the ``.init()`` callback has been moved
  to the worker process.

* The parameter ``tcp_keepalive_time`` is supported on MacOS.

* Duration parameters can optionally take a unit, with the same syntax as
  duration units in VCL. Example: ``param.set default_grace 1h``.

* Calls to ``VRT_CacheReqBody()`` and ``std.cache_req_body`` from outside
  client vcl subs now fail properly instead of triggering an
  assertion failure (3846_).

* New ``"B"`` string for the package branch in ``VCS_String()``. For the 7.2.0
  version, it would yield the 7.2 branch.

* The Varnish version and branch are available in ``varnishtest`` through the
  ``${pkg_version}`` and ``${pkg_branch}`` macros.

* New ``${topsrc}`` macro in ``varnishtest -i`` mode.

* New ``process pNAME -match-text`` command in ``varnishtest`` to expect
  text matching a regular expression on screen.

* New ``filewrite [-a]`` command in ``varnishtest`` to put or append a string
  into a file.

* The new ``vcc_feature`` bits parameter replaces previous ``vcc_*`` boolean
  parameters. The latter still exist as deprecated aliases.

* The ``-k`` option from ``varnishlog`` is now supported by ``varnishncsa``.

* New functions ``std.now()`` and ``std.timed_call()`` in vmod_std.

* New ``MAIN.shm_bytes`` counter.

* A ``req.http.via`` header is set before entering ``vcl_recv``. Via headers
  are generated using the ``server.identity`` value. It defaults to the host
  name and can be turned into a pseudonym with the ``varnishd -i`` option.
  Via headers are appended in both directions, to work with other hops that
  may advertise themselves.

* A ``resp.http.via`` header is no longer overwritten by varnish, but
  rather appended to.

* The ``server.identity`` syntax is now limited to a "token" as defined in
  the HTTP grammar to be suitable for Via headers.

* In ``varnishtest`` a Varnish instance will use its VTC instance name as its
  instance name (``varnishd -i``) by default for predictable Via headers in
  test cases.

* VMOD and VEXT authors can use functions from ``vnum.h``.

* Do not filter pseudo-headers as regular headers (VSV00009_ / 3830_).

* The termination rules for ``WRK_BgThread()`` were relaxed to allow VMODs to
  use it.

* ``(struct worker).handling`` has been moved to the newly introduced
  ``struct wrk_vpi`` and replaced by a pointer to it, as well as
  ``(struct vrt_ctx).handling`` has been replaced by that pointer.

  ``struct wrk_vpi`` is for state at the interface between VRT and VGC
  and, in particular, is not const as ``struct vrt_ctx`` aka
  ``VRT_CTX``.

* Panics now contain information about VCL source files and lines.

* The ``Begin`` log record has a 4th field for subtasks like ESI sub-requests.

* The ``-E`` option for log utilities now works as documented, with any type
  of sub-task based on the ``Begin[4]`` field. This covers ESI like before,
  and sub-tasks spawned by VMODs (provided that they log the new field).

* No more ``req.http.transfer-encoding`` for ESI sub-requests.

* New ``tools/coccinelle/vcocci.sh`` refactoring script for internal use.

* The thread pool reserve is now limited to tasks that can be queued. A
  backend background fetch is no longer eligible for queueing. It would
  otherwise slow a grace hit down significantly when thread pools are
  saturated.

* The unused ``fetch_no_thread`` counter was renamed to ``bgfetch_no_thread``
  because regular backend fetch tasks are always scheduled.

* The macros ``FEATURE()``, ``EXPERIMENT()``, ``DO_DEBUG()``,
  ``MGT_FEATURE()``, ``MGT_EXPERIMENT()``, ``MGT_DO_DEBUG()`` and
  ``MGT_VCC_FEATURE()`` now return a boolean value (``0`` or ``1``)
  instead of the (private) flag value.

* There is a new ``contrib/`` directory in the Varnish source tree. The first
  contribution is a ``varnishstatdiff`` script.

* A regression in the transport code led MAIN.client_req to be incremented
  for requests coming back from the waiting list, it was fixed.  (3841_)

.. _3830: https://github.com/varnishcache/varnish-cache/issues/3830
.. _3841: https://github.com/varnishcache/varnish-cache/pull/3841
.. _3846: https://github.com/varnishcache/varnish-cache/issues/3846
.. _VSV00009: https://varnish-cache.org/security/VSV00009.html

================================
Varnish Cache 7.1.0 (2022-03-15)
================================

* The ``cookie.format_rfc1123()`` function was renamed to
  ``cookie.format_date()``, and the former was retained as a
  deprecated alias.

* The VCC file ``$Alias`` stanza has been added to support vmod alias
  functions/methods.

* VCC now supports alias symbols.

* There is a new ``experimental`` parameter that is identical to the
  ``feature`` parameter, except that it guards features that may not
  be considered complete or stable. An experimental feature may be
  promoted to a regular feature or dropped without being considered a
  breaking change.

* ESI includes now support the ``onerror="continue"`` attribute of
  ``<esi:include/>`` tags.

  The ``+esi_include_onerror`` feature flag controls if the attribute
  is honored: If enabled, failure of an include stops ESI processing
  unless the ``onerror="continue"`` attribute was set for it.

  The feature flag is off by default, preserving the existing behavior
  to continue ESI processing despite include failures.

* The deprecated sub-argument of the ``-l`` option was removed, it is
  now a shorthand for the ``vsl_space`` parameter only.

* The ``-T``, ``-M`` and ``-P`` command line options can be used
  multiple times, instead of retaining only the last occurrence.

* The ``debug.xid`` CLI command has been extended to also set and
  query the VXID cache chunk size.

* The ``vtc.barrier_sync()`` VMOD function now also works in ``vcl_init``

* The ``abort`` command in the ``logexpect`` facility of
  ``varnishtest`` can now be used to trigger an ``abort()`` to help
  debugging the vsl client library code.

* The ``vtc.vsl()`` and ``vtc.vsl_replay()`` functions have been added
  to the vtc vmod to generate arbitrary log lines for testing.

* The limit of the ``vsl_reclen`` parameter has been corrected.

* Varnish now closes client connections correctly when request body
  processing failed.

* Filter init methods of types ``vdp_init_f`` and ``vfp_init_f``
  gained a ``VRT_CTX`` argument.

* The ``param.set`` CLI command accepts a ``-j`` option. In this case
  the JSON output is the same as ``param.show -j`` of the updated
  parameter.

* A new ``cc_warnings`` parameter contains a subset of the compiler
  flags extracted from ``cc_command``, which in turn grew new
  expansions:

  - ``%d``: the raw default ``cc_command``
  - ``%D``: the expanded default ``cc_command``
  - ``%w``: the ``cc_warnings`` parameter
  - ``%n``: the working directory (``-n`` option)

* For ``return(pipe)``, the backend transactions now emit a Start
  timestamp and both client and backend transactions emit the Process
  timestamp.

* ``http_IsHdr()`` is now exposed as part of the strict ABI for VMODs.

* The ``req.transport`` VCL variable has been added, which returns
  "HTTP/1" or "HTTP/2" as appropriate.

* The ``vtc.workspace_reserve()`` VMOD function now zeroes memory.

* Parameter aliases have been added to facilitate parameter deprecation.

* Two bugs in the catflap facility have been fixed which could trigger
  panics due to the state pointer not being cleared. (3752_, 3755_)

* It is now possible to assign to a ``BODY`` variable either a
  ``STRING`` type or a ``BLOB``.

* When the ``vcl.show`` CLI command is invoked without a parameter, it
  now defaults to the active VCL.

* The reporting of ``logexpect`` events in ``varnishtest`` was
  rearranged for readability.

* Workspace debugging as enabled by the ``+workspace`` debug flag is
  now logged with the corresponding transaction.

* VMODs should now register and unregister fetch and delivery filters
  with ``VRT_AddFilter()`` and ``VRT_RemoveFilter()``.

* ``HSH_purge()`` has been rewritten to properly handle concurrent
  purges on the same object head.

* ``VSL_WriteOpen()``, ``varnishlog`` and ``varnishncsa`` have been
  changed to support writing to stdout with ``-w -`` when not in
  daemon mode.

* In VSL, the case has been optimized that the space remaining in a
  buffer is close to ``vsl_reclen``.

* ``std.ip()`` has been changed to always return a valid (bogo ip)
  fallback if the fallback argument is invalid.

* New VCL variables ``{req,req_top,resp,bereq,beresp,obj}.time`` have
  been added to track when the respective object was born.

* ``VRT_StaticDirector()`` has been added to mark directors with VCL
  lifetime, to avoid the overhead of reference counting.

* Dynamic backends are now reference-counted, and VMOD authors must
  explicitly track assignments with ``VRT_Assign_Backend()``.

* Varnish will use libunwind by default when available at configure
  time, the ``--without-unwind`` configure flag can prevent this and
  fall back to libexecinfo to generate backtraces.

* A new ``debug.shutdown.delay`` command is available in the Varnish
  CLI for testing purposes.

* New utility macros ``vmin[_t]``, ``vmax[_t]`` and ``vlimit[_t]``
  available in ``vdef.h``.

* The macros ``TOSTRAND(s)`` and ``TOSTRANDS(x, ...)`` have been added
  to create a ``struct strands *`` (intended to be used as a
  ``VCL_STANDS``) from a single string ``s`` or ``x`` strings,
  respectively.

  Note that the macros create a compound literal whose scope is the
  enclosing block. Their value must thus only be used within the same
  block (it can be passed to called functions) and must not be
  returned or referenced for use outside the enclosing block.

  As before, ``VRT_AllocStrandsWS()`` or ``VRT_StrandsWS()`` must be
  used to create ``VCL_STRANDS`` with *task* scope for use outside the
  current block.

* A bug in the backend connection handling code has been fixed which
  could trigger an unwarranted assertion failure (3664_).

* ``std.strftime()`` has been added.

* ``Lck_CondWait()`` has lost the timeout argument and now waits
  forever. ``Lck_CondWaitUntil()`` and ``Lck_CondWaitTimeout()`` have
  been added to wait on a condition variable until some point in time
  or until a timeout expires, respectively.

* All mutex locks in core code have been given the
  ``PTHREAD_MUTEX_ERRORCHECK`` attribute.

* ``Host`` and ``Content-Length`` header checks have been moved to
  protocol independent code and thus implicitly extended to HTTP2.

* A potential race on busy objects has been closed.

* Use of the ``ObjGetSpace()`` for synthetic objects has been fixed to
  support stevedores returning less space than requested (as permitted
  by the API).

* The ``FINI_OBJ()`` macro has been added to standardize the common
  pattern of zeroing a mini object and clearing a pointer to it.

* The deprecated ``vsm_space`` parameter was removed.

* The ``varnishtest`` ``err_shell`` commando has been removed after
  having been deprecated since release 5.1.0.

.. _3755: https://github.com/varnishcache/varnish-cache/issues/3755
.. _3752: https://github.com/varnishcache/varnish-cache/issues/3752
.. _3664: https://github.com/varnishcache/varnish-cache/issues/3664

================================
Varnish Cache 7.0.1 (2021-11-23)
================================

* An assertion failure has been fixed which triggered when matching bans
  on non-existing headers (3706_).

* A VCL compilation issue has been fixed when calling builtin functions
  directly (3719_).

* It is now again possible to concatenate static strings to produce
  combined strings of type VCL_REGEX (3721_).

* An issue has been fixed that would cause the VCL dependency checker to
  incorrectly flag VCLs as dependents of other VCLs when using labels,
  preventing them from being discarded (3734_).

* VCLs loaded through CLI or the use of startup CLI scripts (-I option to
  `varnishd`) will, when no active VCL has previously been set, no longer
  automatically set the first VCL loaded to the active VCL. This prevents
  situations where it was possible to make a cold VCL the active VCL
  (3737_).

* There is now a `configure` build-time requirement on working SO_RCVTIMEO
  and SO_SNDTIMEO socket options.

  We no longer check whether they effectively work, so the
  ``SO_RCVTIMEO_WORKS`` feature check has been removed from
  ``varnishtest``.

* The socket option inheritance checks now correctly identifies situations
  where UDS and TCP listening sockets behave differently, and are no
  longer subject to the order the inheritance checks happens to be
  executed (3732_).

* IPv6 listen endpoint address strings are now printed using brackets.

.. _3706: https://github.com/varnishcache/varnish-cache/issues/3706
.. _3719: https://github.com/varnishcache/varnish-cache/issues/3719
.. _3721: https://github.com/varnishcache/varnish-cache/issues/3726
.. _3734: https://github.com/varnishcache/varnish-cache/issues/3734
.. _3737: https://github.com/varnishcache/varnish-cache/pull/3737
.. _3732: https://github.com/varnishcache/varnish-cache/pull/3732

================================
Varnish Cache 7.0.0 (2021-09-15)
================================

* Added convenience ``vrt_null_strands`` and ``vrt_null_blob`` constants.

* New VCL flag syntax ``foo <name> +bar -baz { ... }``, starting with ACL
  flags ``log``, ``pedantic`` and ``table``.

* ACLs no longer produce VSL ``VCL_acl`` records by default, this must be
  explicitly enabled with ``acl <name> +log { ... }``.

* ACLs can be compiled into a table format, which runs a little bit
  slower, but compiles much faster for large ACLs.

* ACLs default to ``pedantic`` which is now a per-ACL feature flag.

* New ``glob`` flag for VCL ``include`` (3193_).

* The maximum number of headers for a request or a response in ``varnishtest``
  was increased to 64.

* The backend lock class from struct backend was moved to struct director and
  renamed accordingly.

* New ``%{sec,msec,usec,msec_frac,usec_frac}t`` formats in ``varnishncsa``.

* ``vstrerror()`` was renamed to ``VAS_errtxt()``.

* New ``varnishncsa -j`` option to format for JSON (3595_).

* To skip a test in the *presence* of a feature instead of it absence, a new
  ``feature !<name>`` syntax was added to ``varnishtest``.

* Accept-Ranges headers are no longer generated for passed objects,
  but must either come from the backend or be created in ``vcl_deliver{}``
  (3251_).

* The busyobj ``do_pass`` flag is gone in favor of ``uncacheable``.

* The objcore flag ABANDON was renamed to CANCEL.

* 'Scientific Notation' numbers like 6.62607004e-34 are no longer
  supported in VCL.  (The preparation of RFC8941 made it clear that
  there are neither reason nor any need to support scientific notation
  in context of HTTP headers.

* New ``tunnel`` command in ``varnishtest`` to gain the ability to
  shape traffic between two peers without having to change their
  implementation.

* Global VCL symbols can be defined after use (3555_).

* New ``req.hash_ignore_vary`` flag in VCL.

* ``varnishtest`` can register macros backed by functions, which is the case
  for ``${date}`` and the brand new ``${string,<action>[,<args>...]}`` macro
  (3627_).

* Migration to pcre2 with extensive changes to the VRE API, parameters renamed
  to ``pcre2_match_limit`` and ``pcre2_depth_limit``, and the addition of a
  new ``pcre2_jit_compilation`` parameter. The ``varnishtest`` undocumented
  feature check ``pcre_jit`` is gone (3635_). This change is transparent at
  the VRT layer and only affects direct VRE consumers.

* New inverted mode in ``vtc-bisect.sh`` to find the opposite of regressions.

* The default values for ``workspace_client``, ``workspace_backend`` and
  ``vsl_buffer`` on 64bit systems were increased to respectively 96kB, 96kB
  and 16kB (3648_).

* The deprecated ``WS_Inside()`` was replaced with ``WS_Allocated()`` and
  ``WS_Front()`` was removed.

* VCL header names can be quoted, for example ``req.http."valid.name"``.

* Added ``VRT_UnsetHdr()`` and removed ``vrt_magic_string_unset``.

* Removed deprecated ``STRING_LIST`` in favor of ``STRANDS``. All functions
  that previously took a ``STRING_LIST`` had ``const char *, ...`` arguments,
  they now take ``const char *, VCL_STRANDS`` arguments. The magic cookie
  ``vrt_magic_string_end`` is gone and ``VRT_CollectStrands()`` was renamed to
  ``VRT_STRANDS_string()``.

* The default value for ``thread_pool_stack`` was increased to 80kB for 64bit
  systems and 64kB for 32bit to accomodate the PCRE2 jit compiler.

* Removed deprecated ``VSB_new()`` and ``VSB_delete()``, which resulted in a
  major soname bump of libvarnishapi to 3.0.0, instead of the 2.7.0 version
  initially planned.

* The default workdir (the default ``-n`` argument) is now ``/var/run``
  instead of ``${prefix}/var`` (3672_). Packages usually configure this to
  match local customs.

* The minimum ``session_workspace`` is now 384 bytes

* Emit minimal 500 response if ``vcl_synth`` fails (3441_).

* New ``--enable-coverage`` configure flag, and renovated sanitizer setup.

* New feature checks in ``varnishtest``: ``sanitizer``, ``asan``, ``lsan``,
  ``msan``, ``ubsan`` and ``coverage``.

* New ``--enable-workspace-emulator`` configure flag to swap the worksapce
  implementation with a sparse one ideal for fuzzing (3644_).

* Strict comparison of items from the HTTP grammar (3650_).

* New request body h2 window handling using a buffer to avoid stalling an
  entire h2 session until the relevant stream starts consuming DATA frames.
  As a result the minimum value for ``h2_initial_window_size`` is now 65535B
  to avoid running out of buffer with a negative window that was simpler to
  not tolerate, and a new ``h2_rxbuf_storage`` parameter was added (3661_).

* ``SLT_Hit`` now includes streaming progress when relevant.

* The ``http_range_support`` adds consistency checks for pass transactions
  (3673_).

* New ``VNUM_uint()`` and ``VNUM_hex()`` functions geared at token parsing.

.. _3193: https://github.com/varnishcache/varnish-cache/issues/3193
.. _3251: https://github.com/varnishcache/varnish-cache/issues/3251
.. _3441: https://github.com/varnishcache/varnish-cache/issues/3441
.. _3555: https://github.com/varnishcache/varnish-cache/issues/3555
.. _3595: https://github.com/varnishcache/varnish-cache/issues/3595
.. _3627: https://github.com/varnishcache/varnish-cache/issues/3627
.. _3635: https://github.com/varnishcache/varnish-cache/issues/3635
.. _3644: https://github.com/varnishcache/varnish-cache/issues/3644
.. _3648: https://github.com/varnishcache/varnish-cache/issues/3648
.. _3650: https://github.com/varnishcache/varnish-cache/issues/3650
.. _3661: https://github.com/varnishcache/varnish-cache/issues/3661
.. _3672: https://github.com/varnishcache/varnish-cache/issues/3672
.. _3673: https://github.com/varnishcache/varnish-cache/issues/3673

================================
Varnish Cache 6.6.0 (2021-03-15)
================================

* Body bytes accounting has been fixed to always represent the number
  of bodybytes moved on the wire, exclusive of protocol-specific
  overhead like HTTP/1 chunked encoding or HTTP/2 framing.

  This change affects counters like

  - ``MAIN.s_req_bodybytes``,

  - ``MAIN.s_resp_bodybytes``,

  - ``VBE.*.*.bereq_bodybytes`` and

  - ``VBE.*.*.beresp_bodybytes``

  as well as the VSL records

  - ``ReqAcct``,

  - ``PipeAcct`` and

  - ``BereqAcct``.

* ``VdpAcct`` log records have been added to output delivery filter
  (VDP) accounting details analogous to the existing ``VfpAcct``. Both
  tags are masked by default.

* Many filter (VDP/VFP) related signatures have been changed:

  - ``vdp_init_f``

  - ``vdp_fini_f``

  - ``vdp_bytes_f``

  - ``VDP_bytes()``

  as well as ``struct vdp_entry`` and ``struct vdp_ctx``

  ``VFP_Push()`` and ``VDP_Push()`` are no longer intended for VMOD
  use and have been removed from the API.

* The VDP code is now more strict about ``VDP_END``, which must be
  sent down the filter chain at most once.

* Core code has been changed to ensure for most cases that ``VDP_END``
  gets signaled with the object's last bytes, rather than with an
  extra zero-data call.

* Reason phrases for more HTTP Status codes have been added to core
  code.

* Connection pooling behavior has been improved with respect to
  ``Connection: close`` (3400_, 3405_).

* Handling of the ``Keep-Alive`` HTTP header as hop-by-hop has been
  fixed (3417_).

* Handling of hop-by-hop headers has been fixed for HTTP/2 (3416_).

* The stevedore API has been changed:

  - ``OBJ_ITER_FINAL`` has been renamed to ``OBJ_ITER_END``

  - ``ObjExtend()`` signature has been changed to also cover the
    ``ObjTrimStore()`` use case and

  - ``ObjTrimStore()`` has been removed.

* The ``verrno.h`` header file has been removed and merged into
  ``vas.h``

* The connection close reason has been fixed to properly report
  ``SC_RESP_CLOSE`` / ``resp_close`` where previously only
  ``SC_REQ_CLOSE`` / ``req_close`` was reported.

* Unless the new ``validate_headers`` feature is disabled, all newly
  set headers are now validated to contain only characters allowed by
  RFC7230. A (runtime) VCL failure is triggered if not (3407_).

* ``VRT_ValidHdr()`` has been added for vmods to conduct the same
  check as the ``validate_headers`` feature, for example when headers
  are set by vmods using the ``cache_http.c`` Functions like
  ``http_ForceHeader()`` from untrusted input.

* The shard director now supports reconfiguration (adding/removing
  backends) of several instances without any special ordering
  requirement.

* Calling the shard director ``.reconfigure()`` method is now
  optional. If not called explicitly, any shard director backend
  changes are applied at the end of the current task.

* Shard director ``Error`` log messages with ``(notice)`` have been
  turned into ``Notice`` log messages.

* All shard ``Error`` and ``Notice`` messages now use the unified
  prefix ``vmod_directors: shard %s``.

* In the shard director, use of parameter sets with ``resolve=NOW``
  has been fixed.

* Performance of log-processing tools like ``varnishlog`` has been
  improved by using ``mmap()`` if possible when reading from log
  files.

* An assertion failure has been fixed which could be triggered when a
  request body was used with restarts (3433_, 3434_).

* A signal handling bug in the Varnish Utility API (VUT) has been
  fixed which caused log-processing utilities to perform poorly after
  a signal had been received (3436_).

* The ``client.identity`` variable is now accessible on the backend
  side.

* Client and backend finite state machine internals (``enum req_step``
  and ``enum fetch_step``) have been removed from ``cache.h``.

* Three new ``Timestamp`` VSL records have been added to backend
  request processing:

  - The ``Process`` timestamp after ``return(deliver)`` or
    ``return(pass(x))`` from ``vcl_backend_response``,

  - the ``Fetch`` timestamp before a backend connection is requested
    and

  - the ``Connected`` timestamp when a connection to a regular backend
    (VBE) is established, or when a recycled connection was selected for
    reuse.

* The VRT backend interface has been changed:

  - ``struct vrt_endpoint`` has been added describing a UDS or TCP
    endpoint for a backend to connect to.

    Endpoints also support a preamble to be sent with every new
    connection.

  - This structure needs to be passed via the ``endpoint`` member of
    ``struct vrt_backend`` when creating backends with
    ``VRT_new_backend()`` or ``VRT_new_backend_clustered()``.

* ``VRT_Endpoint_Clone()`` has been added to facilitate working with
  endpoints.

* The variables ``bereq.is_hitpass`` and ``bereq.is_hitmiss`` have
  been added to the backend side matching ``req.is_hitpass`` and
  ``req.is_hitmiss`` on the client side.

* The ``set_ip_tos()`` function from the bundled ``std`` vmod now sets
  the IPv6 Taffic Class (TCLASS) when used on an IPv6 connection.

* A bug has been fixed which could lead to varnish failing to start
  after updates due to outdated content of the ``vmod_cache``
  directory (3243_).

* An issue has been addressed where using VCL with a high number of
  literal strings could lead to prolonged c-compiler runtimes since
  Varnish-Cache 6.3 (3392_).

* The ``MAIN.esi_req`` counter has been added as a statistic of the
  number of ESI sub requests created.

* The ``vcl.discard`` CLI command can now be used to discard more than
  one VCL with a single command, which succeeds only if all given VCLs
  could be discarded (atomic behavior).

* The ``vcl.discard`` CLI command now supports glob patterns for vcl names.

* The ``vcl.deps`` CLI command has been added to output dependencies
  between VCLs (because of labels and ``return(vcl)`` statements).

* The ``FetchError`` log message ``Timed out reusing backend
  connection`` has been renamed to ``first byte timeout (reused
  connection)`` to clarify that it is emit for effectively the same
  reason as ``first byte timeout``.

* Long strings in VCL can now also be denoted using ``""" ... """`` in
  addition to the existing ``{" ... "}``.

* The ``pdiff()`` function declaration has been moved from ``cache.h``
  to ``vas.h``.

* The interface for private pointers in VMODs has been changed:

  - The ``free`` pointer in ``struct vmod_priv`` has been replaced
    with a pointer to ``struct vmod_priv_methods``, to where the
    pointer to the former free callback has been moved as the ``fini``
    member.

  - The former free callback type has been renamed from
    ``vmod_priv_free_f`` to ``vmod_priv_fini_f`` and as gained a
    ``VRT_CTX`` argument

* The ``MAIN.s_bgfetch`` counter has been added as a statistic on the
  number of background fetches issues.

* Various improvements have been made to the ``varnishtest`` facility:

  - the ``loop`` keyword now works everywhere

  - HTTP/2 logging has been improved

  - Default HTTP/2 parameters have been tweaked (3442_)

  - Varnish listen address information is now available by default in
    the macros ``${vNAME_addr}``, ``${vNAME_port}`` and
    ``${vNAME_sock}``. Macros by the names ``${vNAME_SOCKET_*}``
    contain the address information for each listen socket as created
    with the ``-a`` argument to ``varnishd``.

  - Synchronization points for counters (VSCs) have been added as
    ``varnish vNAME -expect PATTERN OP PATTERN``

  - varnishtest now also works with IPv6 setups

  - ``feature ipv4`` and ``feature ipv6`` can be used to control
    execution of test cases which require one or the other protocol.

  - haproxy arguments can now be externally provided through the
    ``HAPROXY_ARGS`` variable.

  - logexpect now supports alternatives with the ``expect ? ...`` syntax
    and negative matches with the ``fail add ...`` and ``fail clear``
    syntax.

  - The overall logexpect match expectation can now be inverted using
    the ``-err`` argument.

  - Numeric comparisons for HTTP headers have been added: ``-lt``,
    ``-le``, ``-eq``, ``-ne``, ``-ge``, ``-gt``

  - ``rxdata -some`` has been fixed.

* The ``ban_cutoff`` parameter now refers to the overall length of the
  ban list, including completed bans, where before only non-completed
  ("active") bans were counted towards ``ban_cutoff``.

* A race in the round-robin director has been fixed which could lead
  to backend requests failing when backends in the director were sick
  (3473_).

* A race in the probe management has been fixed which could lead to a
  panic when VCLs changed temperature in general and when
  ``vcl.discard`` was used in particular (3362_).

* A bug has been fixed which lead to counters (VSCs) of backends from
  cold VCLs being presented (3358_).

* A bug in ``varnishncsa`` has been fixed which could lead to it
  crashing when header fields were referenced which did not exist in
  the processed logs (3485_).

* For failing PROXY connections, ``SessClose`` now provides more
  detailed information on the cause of the failure.

* The ``std.ban()`` and ``std.ban_error()`` functions have been added
  to the ``std`` vmod, allowing VCL to check for ban errors.

* Use of the ``ban()`` built-in VCL command is now deprecated.

* The source tree has been reorganized with all vmods now moved to a
  single ``vmod`` directory.

* ``vmodtool.py`` has been improved to simplify Makefiles when many
  VMODs are built in a single directory.

* The ``VSA_getsockname()`` and ``VSA_getpeername()`` functions have
  been added to get address information of file descriptors.

* ``varnishd`` now supports the ``-b none`` argument to start with
  only the builtin VCL and no backend at all (3067_).

* Some corner cases of IPv6 support in ``varnishd`` have been fixed.

* ``vcl_pipe {}``: ``return(synth)`` and vmod private state support
  have been fixed. Trying to use ``std.rollback()`` from ``vcl_pipe``
  now results in VCL failure (3329_, 3330_, 3385_).

* The ``bereq.xid`` variable is now also available in ``vcl_pipe {}``

* The ``VRT_priv_task_get()`` and ``VRT_priv_top_get()`` functions
  have been added to VRT to allow vmods to retrieve existing
  ``PRIV_TASK`` / ``PRIV_TOP`` private pointers without creating any.

* ``varnishstat`` now avoids display errors of gauges which previously
  could underflow to negative values, being displayed as extremely
  high positive values.

  The ``-r`` option and the ``r`` key binding have been added to
  return to the previous behavior. When raw mode is active in
  ``varnishstat`` interactive (curses) mode, the word ``RAW`` is
  displayed at the right hand side in the lower status line.

* The ``VSC_IsRaw()`` function has been added to ``libvarnishapi`` to
  query if a gauge is being returned raw or adjusted.

* The ``busy_stats_rate`` feature flag has been added to ensure
  statistics updates (as configured using the ``thread_stats_rate``
  parameter) even in scenarios where worker threads never run out
  of tasks and may remain forever busy.

* ``ExpKill`` log (VSL) records are now masked by default. See the
  ``vsl_mask`` parameter.

* A bug has been fixed which could lead to panics when ESI was used
  with ESI-aware VMODs were used because ``PRIV_TOP`` vmod private
  state was created on a wrong workspace (3496_).

* The ``VCL_REGEX`` data type is now supported for VMODs, allowing
  them to use regular expression literals checked and compiled by the
  VCL compiler infrastructure.

  Consequently, the ``VRT_re_init()`` and ``VRT_re_fini()`` functions
  have been removed, because they are not required and their use was
  probably wrong anyway.

* The ``filter_re``, ``keep_re`` and ``get_re`` functions from the
  bundled ``cookie`` vmod have been changed to take the ``VCL_REGEX``
  type. This implies that their regular expression arguments now need
  to be literal, whereas before they could be taken from some other
  variable or function returning ``VCL_STRING``.

  Note that these functions never actually handled _dynamic_ regexen,
  the string passed with the first call was compiled to a regex, which
  was then used for the lifetime of the respective VCL.

* The ``%{X}T`` format has been added to ``varnishncsa``, which
  generalizes ``%D`` and ``%T``, but also support milliseconds
  (``ms``) output.

* Error handling has been fixed when vmod functions/methods with
  ``PRIV_TASK`` arguments were wrongly called from the backend side
  (3498_).

* The ``varnishncsa`` ``-E`` argument to show ESI requests has been
  changed to imply ``-c`` (client mode).

* Error handling and performance of the VSL (shared log) client code
  in ``libvarnishapi`` have been improved (3501_).

* ``varnishlog`` now supports the ``-u`` option to write to a file
  specified with ``-w`` unbuffered.

* Comparisons of numbers in VSL queries have been improved to match
  better the behavior which is likely expected by users who have not
  read the documentation in all detail (3463_).

* A bug in the ESI code has been fixed which could trigger a panic
  when no storage space was available (3502_).

* The ``resp.proto`` variable is now read-only as it should have been
  for long.

* ``VTCP_open()`` has been fixed to try all possible addresses from
  the resolver before giving up (3509_). This bug could cause
  confusing error messages (3510_).

* ``VRT_synth_blob()`` and ``VRT_synth_strands()`` have been
  added. The latter should now be used instead of ``VRT_synth_page()``.

* The ``VCL_SUB`` data type is now supported for VMODs to save
  references to subroutines to be called later using
  ``VRT_call()``. Calls from a wrong context (e.g. calling a
  subroutine accessing ``req`` from the backend side) and recursive
  calls fail the VCL.

  See `VMOD - Varnish Modules`_ in the Reference Manual.

.. _VMOD - Varnish Modules: https://varnish-cache.org/docs/trunk/reference/vmod.html

  VMOD functions can also return the ``VCL_SUB`` data type for calls
  from VCL as in ``call vmod.returning_sub();``.

* ``VRT_check_call()`` can be used to check if a ``VRT_call()`` would
  succeed in order to avoid the potential VCL failure in case it would
  not.

  It returns ``NULL`` if ``VRT_call()`` would make the call or an
  error string why not.

* ``VRT_handled()`` has been added, which is now to be used instead of
  access to the ``handling`` member of ``VRT_CTX``.

* The session close reason logging/statistics for HTTP/2 connections
  have been improved (3393_)

* ``varnishadm`` now has the ``-p`` option to disable readline support
  for use in scripts and as a generic CLI connector.

* A log (VSL) ``Notice`` record is now emitted whenever more than
  ``vary_notice`` variants are encountered in the cache for a specific
  hash. The new ``vary_notice`` parameter defaults to 10.

* The modulus operator ``%`` has been added to VCL.

* ``return(retry)`` from ``vcl_backend_error {}`` now correctly resets
  ``beresp.status`` and ``beresp.reason`` (3525_).

* Handling of the ``gunzip`` filter with ESI has been fixed (3529_).

* A bug where the ``threads_limited`` counter could be increased
  without reason has been fixed (3531_).

* All varnish tools using the VUT library utilities for argument
  processing now support the ``--optstring`` argument to return a
  string suitable for use with ``getopts`` from shell scripts.

* An issue with high CPU consumption when the maximum number of
  threads was reached has been fixed (2942_, 3531_)

* HTTP/2 streams are now reset for filter chain (VDP) errors.

* The task priority of incoming connections has been fixed.

* An issue has been addressed where the watchdog facility could
  misfire when tasks are queued.

* The builtin VCL has been reworked: VCL code has been split into
  small subroutines, which custom VCL can prepend custom code to.

  This allows for better integration of custom VCL and the built-in
  VCL and better reuse.

.. _2942: https://github.com/varnishcache/varnish-cache/issues/2942
.. _3067: https://github.com/varnishcache/varnish-cache/issues/3067
.. _3243: https://github.com/varnishcache/varnish-cache/issues/3243
.. _3329: https://github.com/varnishcache/varnish-cache/issues/3329
.. _3330: https://github.com/varnishcache/varnish-cache/issues/3330
.. _3358: https://github.com/varnishcache/varnish-cache/issues/3358
.. _3362: https://github.com/varnishcache/varnish-cache/issues/3362
.. _3385: https://github.com/varnishcache/varnish-cache/issues/3385
.. _3392: https://github.com/varnishcache/varnish-cache/issues/3392
.. _3393: https://github.com/varnishcache/varnish-cache/issues/3393
.. _3400: https://github.com/varnishcache/varnish-cache/issues/3400
.. _3405: https://github.com/varnishcache/varnish-cache/issues/3405
.. _3407: https://github.com/varnishcache/varnish-cache/issues/3407
.. _3416: https://github.com/varnishcache/varnish-cache/issues/3416
.. _3417: https://github.com/varnishcache/varnish-cache/issues/3417
.. _3433: https://github.com/varnishcache/varnish-cache/issues/3433
.. _3434: https://github.com/varnishcache/varnish-cache/issues/3434
.. _3436: https://github.com/varnishcache/varnish-cache/issues/3436
.. _3442: https://github.com/varnishcache/varnish-cache/issues/3442
.. _3463: https://github.com/varnishcache/varnish-cache/issues/3463
.. _3473: https://github.com/varnishcache/varnish-cache/issues/3473
.. _3485: https://github.com/varnishcache/varnish-cache/issues/3485
.. _3496: https://github.com/varnishcache/varnish-cache/issues/3496
.. _3498: https://github.com/varnishcache/varnish-cache/issues/3498
.. _3501: https://github.com/varnishcache/varnish-cache/issues/3501
.. _3502: https://github.com/varnishcache/varnish-cache/issues/3502
.. _3509: https://github.com/varnishcache/varnish-cache/issues/3509
.. _3510: https://github.com/varnishcache/varnish-cache/issues/3510
.. _3525: https://github.com/varnishcache/varnish-cache/issues/3525
.. _3529: https://github.com/varnishcache/varnish-cache/issues/3529
.. _3531: https://github.com/varnishcache/varnish-cache/issues/3531

================================
Varnish Cache 6.5.1 (2020-09-25)
================================

* Bump the VRT_MAJOR_VERSION from 11 to 12, to reflect the API changes
  that went into the 6.5.0 release. This step was forgotten for that
  release.

================================
Varnish Cache 6.5.0 (2020-09-15)
================================

[ABI] marks potentially breaking changes to binary compatibility.

[API] marks potentially breaking changes to source compatibility
(implies [ABI]).

* ``varnishstat`` now has a help screen, available via the ``h`` key
  in curses mode

* The initial ``varnishstat`` verbosity has been changed to ensure any
  fields specified by the ``-f`` argument are visible (2990_)

* Fixed handling of out-of-workspace conditions after
  ``vcl_backend_response`` and ``vcl_deliver`` during filter
  initialization (3253_, 3241_)

* ``PRIV_TOP`` is now thread-safe to support parallel ESI
  implementations

* ``varnishstat`` JSON format (``-j`` option) has been changed:

  * on the top level, a ``version`` identifier has been introduced,
    which will be used to mark future breaking changes to the JSON
    formatting. It will not be used to mark changes to the counters
    themselves.

    The new ``version`` is ``1``.

  * All counters have been moved down one level to the ``counters``
    object.

* ``VSA_BuildFAP()`` has been added as a convenience function to
  build a ``struct suckaddr``

* Depending on the setting of the new ``vcc_acl_pedantic`` parameter,
  VCC now either emits a warning or fails if network numbers used in
  ACLs do not have an all-zero host part.

  For ``vcc_acl_pedantic`` off, the host part is fixed to all-zero and
  that fact logged with the ``ACL`` VSL tag.

* Fixed error handling during object creation after
  ``vcl_backend_response`` (3273_)

* ``obj.can_esi`` has been added to identify if the response can be
  ESI processed (3002_)

* ``resp.filters`` now contains a correct value when the
  auto-determined filter list is read (3002_)

* It is now a VCL (runtime) error to write to ``resp.do_*`` and
  ``beresp.do_*`` fields which determine the filter list after setting
  ``resp.filters`` and ``beresp.filters``, respectively

* Behavior for 304 responses was changed not to update
  the ``Content-Encoding`` response header of the stored object.

* [ABI] ``struct vfp_entry`` and ``struct vdp_ctx`` changed

* [API] VSB_QUOTE_GLOB, which was prematurely added to 6.4, has been
  removed again.

* [API] Add ``VDP_END`` action for delivery processors, which has to
  be sent with or after the last buffer.

* Respect the administrative health for "real" (VBE) backends (3299_)

* Fixed handling of illegal (internal) four-digit response codes and
  with HTTP/2 (3301_)

* Fixed backend connection pooling of closed connections (3266_)

* Added the ``.resolve`` method for the ``BACKEND`` type to resolve
  (determine the "real" backend) a director.

* Improved ``vmodtool`` support for out-of-tree builds

* Added ``VJ_unlink()`` and ``VJ_rmdir()`` jail functions

* Fixed workdir cleanup (3307_)

* Added ``JAIL_MASTER_SYSTEM`` jail level

* The Varnish Jail (least privileges) code for Solaris has been
  largely rewritten. It now reduces privileges even further and thus
  should improve the security of Varnish on Solaris even more.

* The Varnish Jail for Solaris now accepts an optional ``worker=``
  argument which allows to extend the effective privilege set of the
  worker process.

* The shard director and shard director parameter objects should now
  work in ``vcl_pipe {}`` like in ``vcl_backend_* {}`` subs.

* For a failure in ``vcl_recv {}``, the VCL state engine now returns
  right after return from that subroutine. (3303_)

* The shard director now supports weights by scaling the number of
  replicas of each backend on the consistent hashing ring

* Fixed a race in the cache expiry code which could lead to a panic (2999_)

* Added ``VRE_quote()`` to facilitate building literal string matches
  with regular expressions.

* The ``BackendReuse`` VSL (log) tag has been retired and replaced
  with ``BackendClose``, which has been changed to contain either
  ``close`` or ``recycle`` to signify whether the connection was
  closed or returned to a pool for later reuse.

* ``BackendOpen`` VSL entries have been changed to contain ``reuse``
  or ``connect`` in the last column to signify whether the connection
  was reused from a pool or newly opened.

* ``std.rollback()`` of backend requests with ``return(retry)`` has
  been fixed (3353_)

* ``FetchError`` logs now differentiate between ``No backend`` and
  "none resolved" as ``Director %s returned no backend``

* Added ``VRT_DirectorResolve()`` to resolve a director

* Improved VCC handling of symbols and, in particular, type methods

* Fixed use of the shard director from ``vcl_pipe {}`` (3361_)

* Handle recursive use of vcl ``include`` (3360_)

* VCL: Added native support for BLOBs in structured fields notation
  (``:<base64>:``)

* Fixed handling of the ``Connection:`` header when multiple instances
  of the named headers existed.

* Added support for naming ``PRIV_`` arguments to vmod methods/functions

* The varnish binary heap implementation has been renamed to use the
  ``VBH_`` prefix, complemented with a destructor and added to header
  files for use with vmods (via include of ``vbh.h``).

* A bug in ``vmod_blob`` for base64 decoding with a ``length``
  argument and non-padding decoding has been fixed (3378_)

* Added ``VRT_BLOB_string()`` to ``vrt.h``

* VSB support for dynamic vs. static allocations has been changed:

  For dynamic allocations use::

	VSB_new_auto() + VSB_destroy()

  For preexisting buffers use::

	VSB_init() + VSB_fini()

  ``VSB_new()`` + ``VSB_delete()`` are now deprecated.

* ``std.blobread()`` has been added

* New ``MAIN.beresp_uncacheable`` and ``MAIN.beresp_shortlived``
  counters have been added.

* The ``I``, ``X`` and ``R`` arguments have been added to the VSC API
  and ``varnishstat`` for inclusion, exclusion and required glob
  patterns on the statistic field names. (3394_)

  * Added the missing ``VSC_OPT_f`` macro and the new ``VSC_OPT_I`` and
    ``VSC_OPT_X`` to libvarnishapi headers.

  * Added ``-I`` and ``-X`` options to ``varnishstat``.

* Overhaul of the workspace API

  * The previously deprecated ``WS_Reserve()`` has been removed
  * The signature of ``WS_Printf()`` has been changed to return
    ``const char *`` instead of ``void *`` (we do not consider this a
    breaking change).
  * Add ``WS_ReservationSize()``
  * ``WS_Front()`` is now deprecated and replaced by ``WS_Reservation()``

* Handle a workspace overflow in ``VRY_Validate()`` (3319_)

* Fixed the backend probe ``.timeout`` handling for "dripping" responses (3402_)

* New ``VARNISH_VMODS_GENERATED()`` macro in ``varnish.m4``.

* Prevent pooling of a ``Connection: close`` backend response.

  When this header is present, be it sent by the backend or added in
  ``vcl_backend_response {}``, varnish closes the connection after the
  current request. (3400_)

.. _2990: https://github.com/varnishcache/varnish-cache/issues/2990
.. _2999: https://github.com/varnishcache/varnish-cache/issues/2999
.. _3002: https://github.com/varnishcache/varnish-cache/issues/3002
.. _3241: https://github.com/varnishcache/varnish-cache/issues/3241
.. _3253: https://github.com/varnishcache/varnish-cache/issues/3253
.. _3266: https://github.com/varnishcache/varnish-cache/issues/3266
.. _3273: https://github.com/varnishcache/varnish-cache/issues/3273
.. _3299: https://github.com/varnishcache/varnish-cache/issues/3299
.. _3301: https://github.com/varnishcache/varnish-cache/issues/3301
.. _3303: https://github.com/varnishcache/varnish-cache/issues/3303
.. _3307: https://github.com/varnishcache/varnish-cache/issues/3307
.. _3319: https://github.com/varnishcache/varnish-cache/issues/3319
.. _3353: https://github.com/varnishcache/varnish-cache/issues/3353
.. _3360: https://github.com/varnishcache/varnish-cache/issues/3360
.. _3361: https://github.com/varnishcache/varnish-cache/issues/3361
.. _3378: https://github.com/varnishcache/varnish-cache/issues/3378
.. _3394: https://github.com/varnishcache/varnish-cache/issues/3394
.. _3400: https://github.com/varnishcache/varnish-cache/issues/3400
.. _3402: https://github.com/varnishcache/varnish-cache/issues/3402

================================
Varnish Cache 6.4.0 (2020-03-16)
================================

* The ``MAIN.sess_drop`` counter is gone.

* New configure switch: --with-unwind. Alpine linux appears to offer a
  ``libexecinfo`` implementation that crashes when called by Varnish, this
  offers the alternative of using ``libunwind`` instead.

* backend ``none`` was added for "no backend".

* ``std.rollback(bereq)`` is now safe to use, fixed bug 3009_

* Fixed ``varnishstat``, ``varnishtop``, ``varnishhist`` and
  ``varnishadm`` handling INT, TERM and HUP signals (bugs 3088_ and
  3229_)

* The hash algorithm of the ``hash`` director was changed, so backend
  selection will change once only when upgrading. Users of the
  ``hash`` director are advised to consider using the ``shard``
  director, which, amongst other advantages, offers more stable
  backend selection through consistent hashing.

* Log records can safely have empty fields or fields containing blanks if
  they are delimited by "double quotes". This was applied to ``SessError``
  and ``Backend_health``.

* It is now possible for VMOD authors to customize the connection pooling
  of a dynamic backend. A hash is now computed to determine uniqueness and
  a backend declaration can contribute arbitrary data to influence the pool.

* The option ``varnishtest -W`` is gone, the same can be achieved with
  ``varnishtest -p debug=+witness``. A ``witness.sh`` script is available
  in the source tree to generate a graphviz dot file and detect potential
  lock cycles from the test logs.

* The ``Process`` timestamp for ``vcl_synth {}`` was wrongly issued
  before the VCL subroutine, now it gets emitted after VCL returns for
  consistency with ``vcl_deliver {}``.

* Latencies for newly created worker threads to start work on
  congested systems have been improved.

* ``VRB_Iterate()`` signature has changed

* ``VRT_fail()`` now also works from director code

* Deliberately closing backend requests through ``return(abandon)``,
  ``return(fail)`` or ``return(error)`` is no longer accounted as a
  fetch failure

* Fixed a bug which could cause probes not to run

* The ``if-range`` header is now handled, allowing clients to conditionally
  request a range based on a date or an ETag.

* Introduced ``struct reqtop`` to hold information on the ESI top
  request and ``PRIV_TOP``, fixed regression 3019_

* Allow numerical expressions in VCL to be negative / negated

* Add vi-stype CTRL-f / CTRL-b for page down/up to interactive
  varnishstat

* Fixed wrong handling of an out-of-workspae condition in the proxy
  vmod and in the workspace allocator, bug 3131_

* Raised the minimum for the ``vcl_cooldown`` parameter to 1s to fix
  bug 3135_

* Improved creation of additional threads when none are available

* Fixed a race between director creation and the ``backend.list`` CLI
  command - see bug 3094_

* Added error handling to avoid panics for workspace overflows during
  session attribute allocation - bug 3145_

* Overloaded the ``+=`` operator to also append to headers

* Fixed set ``*.body`` commands.

* Fixed status for truncated CLI responses, bug 3038_

* New or improved Coccinelle semantic patches that may be useful for
  VMOD or utilities authors.

* Output VCC warnings also for VCLs loaded via the ``varnishd -f``
  option, see bug 3160_

* Improved fetch error handling when stale objects are present in
  cache, see bug 3089_

* Added a ``Notice`` VSL tag (used for ``varnishlog`` logging)

* Always refer to ``sub`` as subroutine in the documentation and error
  messages to avoid confusion with other terms.

* New ``pid`` command in the Varnish CLI, to get the master and optionally
  cache process PIDs, for example from ``varnishadm``.

* Fixed a race that could result in a partial response being served in its
  entirety when it is also compressed with gzip.

* Fixed session close reason reporting and accounting, added ``rx_close_idle``
  counter for separate accounting when ``timeout_idle`` is reached. Also,
  ``send_timeout`` is no longer reported as "remote closed".

* Fixed handling of request bodies for backend retries

* Fix deadlocks when the maximum number of threads has been reached,
  in particular with http/2, see 2418_

* Add more vcl control over timeouts with ``sess.timeout_linger``,
  ``sess.send_timeout`` and ``sess.idle_send_timeout``

* Fix panics due to missing EINVAL handling on MacOS, see 1853_

* Added ``VSLs()`` and ``VSLbs()`` functions for logging ``STRANDS`` to
  VSL

* Fixed cases where a workspace overflow would not result in a VCL
  failure, see 3194_

* Added ``WS_VSB_new()`` / ``WS_VSB_finish()`` for VSBs on workspaces

* Imported ``vmod_cookie`` from `varnish_modules`_

  The previously deprecated function ``cookie.filter_except()`` has
  been removed during import. It was replaced by ``cookie.keep()``

* ``body_status`` and ``req_body_status`` have been collapsed into one
  type. In particular, the ``REQ_BODY_*`` enums now have been replaced
  with ``BS_*``.

.. mention VSB_QUOTE_GLOB ?

* Fixed an old regression of the ``Age:`` header for passes, see bug
  3221_

* Added ``VRT_AllocStrandsWS()`` as a utility function to allocate
  STRANDS on a workspace.

* Reduced compile time of ``vcl_init{}`` / ``vcl_fini{}`` with gcc,
  added ``v_dont_optimize`` attribute macro

* Fixed a case where ``send_timeout`` would have no effect when
  streaming from a backend fetch, see bug 3189_

  *NOTE* Users upgrading varnish should re-check ``send_timeout`` with
  respect to long pass and streaming fetches and watch out for
  increased session close rates.

* Added ``VSB_tofile()`` to ``libvarnishapi``, see 3238_

.. _1853: https://github.com/varnishcache/varnish-cache/issues/1853
.. _2418: https://github.com/varnishcache/varnish-cache/issues/2418
.. _3009: https://github.com/varnishcache/varnish-cache/issues/3009
.. _3019: https://github.com/varnishcache/varnish-cache/issues/3019
.. _3038: https://github.com/varnishcache/varnish-cache/issues/3038
.. _3088: https://github.com/varnishcache/varnish-cache/issues/3088
.. _3089: https://github.com/varnishcache/varnish-cache/issues/3089
.. _3094: https://github.com/varnishcache/varnish-cache/issues/3094
.. _3131: https://github.com/varnishcache/varnish-cache/issues/3131
.. _3135: https://github.com/varnishcache/varnish-cache/issues/3135
.. _3145: https://github.com/varnishcache/varnish-cache/issues/3145
.. _3160: https://github.com/varnishcache/varnish-cache/issues/3160
.. _3189: https://github.com/varnishcache/varnish-cache/issues/3189
.. _3194: https://github.com/varnishcache/varnish-cache/issues/3194
.. _3221: https://github.com/varnishcache/varnish-cache/issues/3221
.. _3229: https://github.com/varnishcache/varnish-cache/issues/3229
.. _3238: https://github.com/varnishcache/varnish-cache/issues/3238
.. _varnish_modules: https://github.com/varnish/varnish-modules

================================
Varnish Cache 6.3.0 (2019-09-15)
================================

In addition to a significant number of bug fixes, these are the most
important changes in 6.3:

* The Host: header is folded to lower-case in the builtin_vcl.

* Improved performance of shared memory statistics counters.

* Synthetic objects created from ``vcl_backend_error {}`` now replace
  existing stale objects as ordinary backend fetches would, unless:

  - abandoning the bereq or

  - leaving ``vcl_backend_error {}`` with ``return (deliver) and
    ``beresp.ttl == 0s`` or

  - there is a waitinglist on the object, in which case, by default,
    the synthetic object is created with ``ttl = 1s`` / ``grace = 5s``
    / ``keep = 5s`` avoid hammering on failing backends
    (note this is existing behavior).

* Retired the ``BackendStart`` log tag - ``BackendOpen`` contains all
  the information from it

APIs / VMODs
------------

* ``WS_Reserve()`` is now deprecated and any use should trigger a
  compiler warning. It is to be replaced by

  - ``WS_ReserveAll()`` to reserve all of the remaining workspace

    It will always leave the workspace reserved even if 0 bytes are
    available, so it must always be followed by a call to
    ``WS_Release()``

  - ``WS_ReserveSize()`` to reserve a fixed amount.

    It will only leave the workspace reserved if the reservation
    request could be fulfilled.

  We provide a script to help automate this change in the
  ``tools/coccinelle`` subdirectory of the source tree.

* The RST references generated by ``vmodtool.py`` have been changed to
  match better the VCL syntax to avoid overhead where references are
  used. The new scheme for a vmod called *name* is:

  * ``$Function``: *name*\ .\ *function*\ ()
  * ``$Object`` constructor: *name*\ .\ *object*\ ()
  * ``$Method``: x\ *object*\ .\ *method*\ ()

  To illustrate, the old references::

    :ref:`vmod_name.function`
    :ref:`vmod_name.obj`
    :ref:`vmod_name.obj.method`

  now are renamed to::

    :ref:`name.function()`
    :ref:`name.obj()`
    :ref:`xobj.method()`

  ``tools/vmod_ref_rename.sh`` is provided to automate this task

================================
Varnish Cache 6.2.0 (2019-03-15)
================================

* Extend JSON support in the CLI (2783_)

* Improve accuracy of statistics (VSC)

* In ``Error: out of workspace`` log entries, the workspace name is
  now reported in lowercase

* Adjust code generator python tools to python 3 and prefer python 3
  over python 2 where available

* Added a thread pool watchdog which will restart the worker process
  if scheduling tasks onto worker threads appears stuck. The new
  parameter ``thread_pool_watchdog`` configures it. (2418_)

* Changed ``ExpKill`` log tags to emit microsecond-precision
  timestamps instead of nanoseconds (2792_)

* Changed the default of the ``thread_pool_watchdog`` parameter
  to 60 seconds to match the ``cli_timeout`` default

* VSB quoted output has been unified to three-digit octal,
  VSB_QUOTE_ESCHEX has been added to prefer hex over octal quoting

* Retired long deprecated parameters (VIP16_). Replacement mapping is:
  ``shm_reclen`` -> ``vsl_reclen``
  ``vcl_dir`` -> ``vcl_path``
  ``vmod_dir`` -> ``vmod_path``

* The width of the columns of the ``backend.list`` cli command output
  is now dynamic.

  For best forward compatibility, we recommend that scripts parse JSON
  output as obtained using the ``-j`` option.

  See release notes for details.

* The format of the ``backend.list -j`` (JSON) cli command output has
  changed.

  See release notes for details.

* The undocumented ``-v`` option to the ``backend.list`` cli command
  has been removed

* Changed the formatting of the ``vcl.list`` command from::

    status	state/temperature	busy	name	[labelinfo]

  to::

    status	state	temperature	busy	name	[<-|->]	[info]

  Column width is now dynamic.

  Field values remain unchanged except for the label information, see
  varnish-cli(7) for details.

* The ban facility has been extended by bans access to obj.ttl,
  obj.age, obj.grace and obj.keep and additional inequality operators.

* Many cache lookup optimizations.

* Display the VCL syntax during a panic.

* Update to the VCL diagrams to include hit-for-miss.

VCL
---

* Added ``req.is_hitmiss`` and ``req.is_hitpass`` (2743_)


bundled vmods
-------------

* Added ``directors.lookup()``

bundled tools
-------------

* Improved varnish log client performance (2788_)

* For ``varnishtest -L``, also keep VCL C source files

* Add ``param.reset`` command to ``varnishadm``

* Add VSL rate limiting (2837_)

  This adds rate limiting to varnishncsa and varnishlog.

* Make it possible to change ``varnishstat`` update rate. (2741_)

C APIs (for vmod and utility authors)
-------------------------------------

* ``libvarnish``: ``VRT_VSA_GetPtr`` renamed to ``VSA_GetPtr``

* Included ``vtree.h`` in the distribution for vmods and
  renamed the red/black tree macros from ``VRB_*`` to ``VRBT_*``
  to disambiguate from the acronym for Varnish Request Body.

  Changed the internal organisation of dynamic PRIVs (``PRIV_TASK``,
  ``PRIV_TOP`` from a list to a red/black tree) for performance.
  (2813_)

* Vmod developers are advised that anything returned by a vmod
  function/method is assumed to be immutable. In other words, a vmod
  `must not` modify any data which was previously returned.

* Tolerate null IP addresses for ACL matches.

* Added ``vstrerror()`` as a safe wrapper for ``strerror()`` to avoid
  a NULL pointer dereference under rare conditions where the latter
  could return NULL. (2815_)

* Varnish-based tools using the VUT interface should now consider
  using the ``VUT_Usage()`` function for consistency

* The name of the `event_function` callback for VCL events in vmods is
  now prefixed by `$Prefix`\ ``_``\ ` if `$Prefix` is defined in the
  ``.vcc`` file, or ``vmod_`` by default.

  So, for example, with ``$Event foo`` and no `$Prefix`, the event
  function will be called ``vmod_foo`` and with ``$Prefix bar`` it
  will be called ``bar_foo``.

* In the `vmodtool`\ -generated ReStructuredText documentation,
  anchors have been renamed

  * from ``obj_``\ `class` to `vmodname`\ ``.``\ `class` for
    constructors and
  * from ``func_``\ `class` to `vmodname`\ ``.``\ `function` for functions and
  * from ``func_``\ `class` to `vmodname`\ ``.``\ `class`\ ``.``\
    `method` for methods,

  repsectively. In short, the anchor is now named equal to VCL syntax
  for constructors and functions and similarly to VCL syntax for methods.

* VRT API has been updated to 9.0

  * ``HTTP_Copy()`` was removed, ``HTTP_Dup()`` and ``HTTP_Clone()`` were added

  * Previously, ``VCL_BLOB`` was implemented as ``struct vmod_priv``,
    which had the following shortcomings:

    * blobs are immutable, but that was not reflected by the ``priv``
      pointer

    * the existence of a free pointer suggested automatic memory
      management, which did never and will not exist for blobs.

    The ``VCL_BLOB`` type is now implemented as ``struct vrt_blob``,
    with the ``blob`` member replacing the former ``priv`` pointer and
    the ``free`` pointer removed.

    A ``type`` member was added for lightweight type checking similar
    to the miniobject ``magic`` member, but in contrast to it,
    ``type`` should never be asserted upon.

    ``VRT_blob()`` was updated accordingly.

  * ``req->req_bodybytes`` was removed. Replacement code snippet::

      AZ(ObjGetU64(req->wrk, req->body_oc, OA_LEN, &u));

  * ``VRT_SetHealth()`` has been removed and ``VRT_SetChanged()``
    added. ``VRT_LookupDirector()`` (only to be called from CLI
    contexts) as been added.

    See release notes for details

* vmodtool has been changed significantly to avoid various name
  clashes. Rather than using literal prefixes/suffixes, vmod authors
  should now (and might have to for making existing code continue to
  compile) use the following macros

  * ``VPFX(name)`` to prepend the vmod prefix (``vmod_`` by default)

  * ``VARGS(name)`` as the name of a function/method's argument
    struct, e.g.::

	VCL_VOID vmod_test(VRT_CTX, struct VARGS(test) *args) { ...

  * ``VENUM(name)`` to access the enum by the name `name`

Fixed bugs
----------

* Fixed ``varnishhist`` display error (2780_)

* Fix ``varnishstat -f`` in curses mode (interactively, without
  ``-1``, 2787_)

* Handle an out-of-workspace condition in HTTP/2 delivery more
  gracefully (2589_)

* Fixed regression introduced just before 6.1.0 release which caused
  an unnecessary incompatibility with VSL files written by previous
  versions. (2790_)

* Fix warmup/rampup of the shard director (2823_)

* Fix VRT_priv_task for calls from vcl_pipe {} (2820_)

* Fix assigning <bool> == <bool> (2809_)

* Fix vmod object constructor documentation in the ``vmodtool.py`` -
  generated RST files

* Fix some stats metrics (vsc) which were wrongly marked as _gauge_

* Fix ``varnishd -I`` (2782_)

* Add error handling for STV_NewObject() (2831_)

* Fix VRT_fail for 'if'/'elseif' conditional expressions (2840_)

.. _2418: https://github.com/varnishcache/varnish-cache/issues/2418
.. _2589: https://github.com/varnishcache/varnish-cache/issues/2589
.. _2741: https://github.com/varnishcache/varnish-cache/pull/2741
.. _2743: https://github.com/varnishcache/varnish-cache/issues/2743
.. _2780: https://github.com/varnishcache/varnish-cache/issues/2780
.. _2782: https://github.com/varnishcache/varnish-cache/issues/2782
.. _2783: https://github.com/varnishcache/varnish-cache/pull/2783
.. _2787: https://github.com/varnishcache/varnish-cache/issues/2787
.. _2788: https://github.com/varnishcache/varnish-cache/issues/2788
.. _2790: https://github.com/varnishcache/varnish-cache/issues/2790
.. _2792: https://github.com/varnishcache/varnish-cache/pull/2792
.. _2809: https://github.com/varnishcache/varnish-cache/issues/2809
.. _2813: https://github.com/varnishcache/varnish-cache/pull/2813
.. _2815: https://github.com/varnishcache/varnish-cache/issues/2815
.. _2820: https://github.com/varnishcache/varnish-cache/issues/2820
.. _2823: https://github.com/varnishcache/varnish-cache/issues/2823
.. _2831: https://github.com/varnishcache/varnish-cache/issues/2831
.. _2837: https://github.com/varnishcache/varnish-cache/pull/2837
.. _2840: https://github.com/varnishcache/varnish-cache/issues/2840
.. _VIP16: https://github.com/varnishcache/varnish-cache/wiki/VIP16%3A-Retire-parameters-aliases

================================
Varnish Cache 6.1.0 (2018-09-17)
================================

* Added -p max_vcl and -p max_vcl_handling for warnings/errors when
  there are too many undiscarded VCL instances. (2713_)

* ``Content-Length`` header is not rewritten in response to a HEAD
  request, allows responses to HEAD requests to be cached
  independently from GET responses.

.. _2713: https://github.com/varnishcache/varnish-cache/issues/2713

VCL
---

* ``return(fail("mumble"))`` can have a string argument that is
  emitted by VCC as an error message if the VCL load fails due to the
  return. (2694_)

* Improved VCC error messages (2696_)

* Fixed ``obj.hits`` in ``vcl_hit`` (had been always 0) (2746_)

* req.ttl is fully supported again

.. _2746: https://github.com/varnishcache/varnish-cache/issues/2746
.. _2696: https://github.com/varnishcache/varnish-cache/issues/2696
.. _2694: https://github.com/varnishcache/varnish-cache/issues/2694

bundled tools
-------------

* ``varnishhist``: Improved test coverage
* ``varnishtest``: Added haproxy CLI send/expect facility

C APIs (for vmod and utility authors)
-------------------------------------

* libvarnishapi so version bumped to 2.0.0 (2718_)

* For VMOD methods/functions with PRIV_TASK or PRIV_TOP arguments, the
  struct vrt_priv is allocated on the appropriate workspace. In the
  out-of-workspace condition, VCL failure is invoked, and the VMOD
  method/function is not called. (2708_)

* Improved support for the VCL STRANDS type, VMOD blob refactored to
  use STRANDS (2745_)

.. _2718: https://github.com/varnishcache/varnish-cache/pull/2718
.. _2745: https://github.com/varnishcache/varnish-cache/issues/2745
.. _2708: https://github.com/varnishcache/varnish-cache/issues/2708

Fixed bugs
----------

* A series of bug fixes related to excessive object accumulation and
  Transient storage use in the hit-for-miss case (2760_, 2754_, 2654_,
  2763_)
* A series of fixes related to Python and the vmodtool (2761_, 2759_,
  2742_)
* UB in varnishhist (2773_)
* Allow to not have randomness in file_id (2436_)
* b64.vtc unstable (2753_)
* VCL_Poll ctx scope (2749_)

.. _2436: https://github.com/varnishcache/varnish-cache/issues/2436
.. _2654: https://github.com/varnishcache/varnish-cache/issues/2654
.. _2742: https://github.com/varnishcache/varnish-cache/issues/2742
.. _2749: https://github.com/varnishcache/varnish-cache/issues/2749
.. _2753: https://github.com/varnishcache/varnish-cache/issues/2753
.. _2754: https://github.com/varnishcache/varnish-cache/issues/2754
.. _2759: https://github.com/varnishcache/varnish-cache/pull/2759
.. _2760: https://github.com/varnishcache/varnish-cache/pull/2760
.. _2761: https://github.com/varnishcache/varnish-cache/issues/2761
.. _2763: https://github.com/varnishcache/varnish-cache/issues/2763
.. _2773: https://github.com/varnishcache/varnish-cache/issues/2773

================================
Varnish Cache 6.0.1 (2018-08-29)
================================

* Added std.fnmatch() (2737_)
* The variable req.grace is back. (2705_)
* Importing the same VMOD multiple times is now allowed, if the file_id
  is identical.

.. _2705: https://github.com/varnishcache/varnish-cache/pull/2705
.. _2737: https://github.com/varnishcache/varnish-cache/pull/2737

varnishstat
-----------

* The counters

  * ``sess_fail_econnaborted``
  * ``sess_fail_eintr``
  * ``sess_fail_emfile``
  * ``sess_fail_ebadf``
  * ``sess_fail_enomem``
  * ``sess_fail_other``

  now break down the detailed reason for session accept failures, the
  sum of which continues to be counted in ``sess_fail``.

VCL and bundled VMODs
---------------------

* VMOD unix now supports the ``getpeerucred(3)`` case.

bundled tools
-------------

* ``varnishhist``: The format of the ``-P`` argument has been changed
  for custom profile definitions to also contain a prefix to match the
  tag against.

* ``varnishtest``: syslog instances now have to start with a capital S.

Fixed bugs which may influence VCL behavior
--------------------------------------------

* When an object is out of grace but in keep, the client context goes
  straight to vcl_miss instead of vcl_hit. The documentation has been
  updated accordingly. (2705_)

Fixed bugs
----------

* Several H2 bugs (2285_, 2572_, 2623_, 2624_, 2679_, 2690_, 2693_)
* Make large integers work in VCL. (2603_)
* Print usage on unknown or missing arguments (2608_)
* Assert error in VPX_Send_Proxy() with proxy backends in pipe mode
  (2613_)
* Holddown times for certain backend connection errors (2622_)
* Enforce Host requirement for HTTP/1.1 requests (2631_)
* Introduction of '-' CLI prefix allowed empty commands to sneak
  through. (2647_)
* VUT apps can be stopped cleanly via vtc process -stop (2649_, 2650_)
* VUT apps fail gracefully when removing a PID file fails
* varnishd startup log should mention version (2661_)
* In curses mode, always filter in the counters necessary for the
  header lines. (2678_)
* Assert error in ban_lurker_getfirst() (2681_)
* Missing command entries in varnishadm help menu (2682_)
* Handle string literal concatenation correctly (2685_)
* varnishtop -1 does not work as documented (2686_)
* Handle sigbus like sigsegv (2693_)
* Panic on return (retry) of a conditional fetch (2700_)
* Wrong turn at cache/cache_backend_probe.c:255: Unknown family
  (2702_, 2726_)
* VCL failure causes TASK_PRIV reference on reset workspace (2706_)
* Accurate ban statistics except for a few remaining corner cases
  (2716_)
* Assert error in vca_make_session() (2719_)
* Assert error in vca_tcp_opt_set() (2722_)
* VCL compiling error on parenthesis (2727_)
* Assert error in HTC_RxPipeline() (2731_)

.. _2285: https://github.com/varnishcache/varnish-cache/issues/2285
.. _2572: https://github.com/varnishcache/varnish-cache/issues/2572
.. _2603: https://github.com/varnishcache/varnish-cache/issues/2603
.. _2608: https://github.com/varnishcache/varnish-cache/issues/2608
.. _2613: https://github.com/varnishcache/varnish-cache/issues/2613
.. _2622: https://github.com/varnishcache/varnish-cache/issues/2622
.. _2623: https://github.com/varnishcache/varnish-cache/issues/2623
.. _2624: https://github.com/varnishcache/varnish-cache/issues/2624
.. _2631: https://github.com/varnishcache/varnish-cache/issues/2631
.. _2647: https://github.com/varnishcache/varnish-cache/issues/2647
.. _2649: https://github.com/varnishcache/varnish-cache/issues/2649
.. _2650: https://github.com/varnishcache/varnish-cache/pull/2650
.. _2651: https://github.com/varnishcache/varnish-cache/pull/2651
.. _2661: https://github.com/varnishcache/varnish-cache/issues/2661
.. _2678: https://github.com/varnishcache/varnish-cache/issues/2678
.. _2679: https://github.com/varnishcache/varnish-cache/issues/2679
.. _2681: https://github.com/varnishcache/varnish-cache/issues/2681
.. _2682: https://github.com/varnishcache/varnish-cache/issues/2682
.. _2685: https://github.com/varnishcache/varnish-cache/issues/2685
.. _2686: https://github.com/varnishcache/varnish-cache/issues/2686
.. _2690: https://github.com/varnishcache/varnish-cache/issues/2690
.. _2693: https://github.com/varnishcache/varnish-cache/issues/2693
.. _2695: https://github.com/varnishcache/varnish-cache/issues/2695
.. _2700: https://github.com/varnishcache/varnish-cache/issues/2700
.. _2702: https://github.com/varnishcache/varnish-cache/issues/2702
.. _2706: https://github.com/varnishcache/varnish-cache/issues/2706
.. _2716: https://github.com/varnishcache/varnish-cache/issues/2716
.. _2719: https://github.com/varnishcache/varnish-cache/issues/2719
.. _2722: https://github.com/varnishcache/varnish-cache/issues/2722
.. _2726: https://github.com/varnishcache/varnish-cache/pull/2726
.. _2727: https://github.com/varnishcache/varnish-cache/issues/2727
.. _2731: https://github.com/varnishcache/varnish-cache/issues/2731

================================
Varnish Cache 6.0.0 (2018-03-15)
================================

Usage
-----

* Fixed implementation of the ``max_restarts`` limit: It used to be one
  less than the number of allowed restarts, it now is the number of
  ``return(restart)`` calls per request.

* The ``cli_buffer`` parameter has been removed

* Added back ``umem`` storage for Solaris descendants

* The new storage backend type (stevedore) ``default`` now resolves to
  either ``umem`` (where available) or ``malloc``.

* Since varnish 4.1, the thread workspace as configured by
  ``workspace_thread`` was not used as documented, delivery also used
  the client workspace.

  We are now taking delivery IO vectors from the thread workspace, so
  the parameter documentation is in sync with reality again.

  Users who need to minimize memory footprint might consider
  decreasing ``workspace_client`` by ``workspace_thread``.

* The new parameter ``esi_iovs`` configures the amount of IO vectors
  used during ESI delivery. It should not be tuned unless advised by a
  developer.

* Support Unix domain sockets for the ``-a`` and ``-b`` command-line
  arguments, and for backend declarations. This requires VCL >= 4.1.

VCL and bundled VMODs
---------------------

* ``return (fetch)`` is no longer allowed in ``vcl_hit {}``, use
  ``return (miss)`` instead. Note that ``return (fetch)`` has been
  deprecated since 4.0.

* Fix behaviour of restarts to how it was originally intended:
  Restarts now leave all the request properties in place except for
  ``req.restarts`` and ``req.xid``, which need to change by design.

* ``req.storage``, ``req.hash_ignore_busy`` and
  ``req.hash_always_miss`` are now accessible from all of the client
  side subs, not just ``vcl_recv{}``

* ``obj.storage`` is now available in ``vcl_hit{}`` and ``vcl_deliver{}``.

* Removed ``beresp.storage_hint`` for VCL 4.1 (was deprecated since
  Varnish 5.1)

  For VCL 4.0, compatibility is preserved, but the implementation is
  changed slightly: ``beresp.storage_hint`` is now referring to the
  same internal data structure as ``beresp.storage``.

  In particular, it was previously possible to set
  ``beresp.storage_hint`` to an invalid storage name and later
  retrieve it back. Doing so will now yield the last successfully set
  stevedore or the undefined (``NULL``) string.

* IP-valued elements of VCL are equivalent to ``0.0.0.0:0`` when the
  connection in question was addressed as a UDS. This is implemented
  with the ``bogo_ip`` in ``vsa.c``.

* ``beresp.backend.ip`` is retired as of VCL 4.1.

* workspace overflows in ``std.log()`` now trigger a VCL failure.

* workspace overflows in ``std.syslog()`` are ignored.

* added ``return(restart)`` from ``vcl_recv{}``.

* The ``alg`` argument of the ``shard`` director ``.reconfigure()``
  method has been removed - the consistent hashing ring is now always
  generated using the last 32 bits of a SHA256 hash of ``"ident%d"``
  as with ``alg=SHA256`` or the default.

  We believe that the other algorithms did not yield sufficiently
  dispersed placement of backends on the consistent hashing ring and
  thus retire this option without replacement.

  Users of ``.reconfigure(alg=CRC32)`` or ``.reconfigure(alg=RS)`` be
  advised that when upgrading and removing the ``alg`` argument,
  consistent hashing values for all backends will change once and only
  once.

* The ``alg`` argument of the ``shard`` director ``.key()`` method has
  been removed - it now always hashes its arguments using SHA256 and
  returns the last 32 bits for use as a shard key.

  Backwards compatibility is provided through `vmod blobdigest`_ with
  the ``key_blob`` argument of the ``shard`` director ``.backend()``
  method:

  * for ``alg=CRC32``, replace::

      <dir>.backend(by=KEY, key=<dir>.key(<string>, CRC32))

    with::

      <dir>.backend(by=BLOB, key_blob=blobdigest.hash(ICRC32,
	blob.decode(encoded=<string>)))

    `Note:` The `vmod blobdigest`_ hash method corresponding to the
    shard director CRC32 method is called **I**\ CRC32

.. _vmod blobdigest: https://code.uplex.de/uplex-varnish/libvmod-blobdigest/blob/master/README.rst

  * for ``alg=RS``, replace::

      <dir>.backend(by=KEY, key=<dir>.key(<string>, RS))

    with::

      <dir>.backend(by=BLOB, key_blob=blobdigest.hash(RS,
	blob.decode(encoded=<string>)))

* The ``shard`` director now offers resolution at the time the actual
  backend connection is made, which is how all other bundled directors
  work as well: With the ``resolve=LAZY`` argument, other shard
  parameters are saved for later reference and a director object is
  returned.

  This enables layering the shard director below other directors.

* The ``shard`` director now also supports getting other parameters
  from a parameter set object: Rather than passing the required
  parameters with each ``.backend()`` call, an object can be
  associated with a shard director defining the parameters. The
  association can be changed in ``vcl_backend_fetch()`` and individual
  parameters can be overridden in each ``.backend()`` call.

  The main use case is to segregate shard parameters from director
  selection: By associating a parameter object with many directors,
  the same load balancing decision can easily be applied independent
  of which set of backends is to be used.

* To support parameter overriding, support for positional arguments of
  the shard director ``.backend()`` method had to be removed. In other
  words, all parameters to the shard director ``.backend()`` method
  now need to be named.

* Integers in VCL are now 64 bits wide across all platforms
  (implemented as ``int64_t`` C type), but due to implementation
  specifics of the VCL compiler (VCC), integer literals' precision is
  limited to that of a VCL real (``double`` C type, roughly 53 bits).

  In effect, larger integers are not represented accurately (they get
  rounded) and may even have their sign changed or trigger a C
  compiler warning / error.

* Add VMOD unix.

* Add VMOD proxy.

Logging / statistics
--------------------

* Turned off PROXY protocol debugging by default, can be enabled with
  the ``protocol`` debug flag.

* added ``cache_hit_grace`` statistics counter.

* added ``n_lru_limited`` counter.

* The byte counters in ReqAcct now show the numbers reported from the
  operating system rather than what we anticipated to send. This will give
  more accurate numbers when e.g. the client hung up early without
  receiving the entire response. Also these counters now show how many
  bytes was attributed to the body, including any protocol overhead (ie
  chunked encoding).

bundled tools
-------------

* ``varnishncsa`` refuses output formats (as defined with the ``-F``
  command line argument) for tags which could contain control or
  binary characters. At the time of writing, these are:
  ``%{H2RxHdr}x``, ``%{H2RxBody}x``, ``%{H2TxHdr}x``, ``%{H2TxBody}x``,
  ``%{Debug}x``, ``%{HttpGarbage}x`` and ``%{Hash}x``

* The vtc ``server -listen`` command supports UDS addresses, as does
  the ``client -connect`` command. vtc ``remote.path`` and
  ``remote.port`` have the values ``0.0.0.0`` and ``0`` when the peer
  address is UDS. Added ``remote.path`` to vtc, whose value is the
  path when the address is UDS, and NULL (matching <undef>) for IP
  addresses.

C APIs (for vmod and utility authors)
-------------------------------------

* We have now defined three API Stability levels: ``VRT``,
  ``PACKAGE``, ``SOURCE``.

* New API namespace rules, see `phk_api_spaces_`

* Rules for including API headers have been changed:
  * many headers can now only be included once
  * some headers require specific include ordering
  * only ``cache.h`` _or_ ``vrt.h`` can be included

* Signatures of functions in the VLU API for bytestream into text
  serialization have been changed

* vcl.h now contains convenience macros ``VCL_MET_TASK_B``,
  ``VCL_MET_TASK_C`` and ``VCL_MET_TASK_H`` for checking
  ``ctx->method`` for backend, client and housekeeping
  (vcl_init/vcl_fini) task context

* vcc files can now contain a ``$Prefix`` stanza to define the prefix
  for vmod function names (which was fixed to ``vmod`` before)

* vcc files can contain a ``$Synopsis`` stanza with one of the values
  ``auto`` or ``manual``, default ``auto``. With ``auto``, a more
  comprehensive SYNOPSIS is generated in the doc output with an
  overview of objects, methods, functions and their signatures. With
  ``manual``, the auto-SYNOPSIS is left out, for VMOD authors who
  prefer to write their own.

* All Varnish internal ``SHA256*`` symbols have been renamed to
  ``VSHA256*``

* libvarnish now has ``VNUM_duration()`` to convert from a VCL
  duration like 4h or 5s

* director health state queries have been merged to ``VRT_Healthy()``

* Renamed macros:
  * ``__match_proto__()`` -> ``v_matchproto_()``
  * ``__v_printflike()`` -> ``v_printflike_()``
  * ``__state_variable__()`` -> ``v_statevariable_()``
  * ``__unused`` -> ``v_unused_``
  * ``__attribute__((__noreturn__)`` -> ``v_noreturn_``

* ENUMs are now fixed pointers per vcl.

* Added ``VRT_blob()`` utility function to create a blob as a copy
  of some chunk of data on the workspace.

* Directors now have their own admin health information and always need to
  have the ``(struct director).admin_health`` initialized to
  ``VDI_AH_*`` (usually ``VDI_AH_HEALTHY``).

Other changes relevant for VMODs
--------------------------------

* ``PRIV_*`` function/method arguments are not excluded from
  auto-generated vmod documentation.

Fixed bugs which may influence VCL behaviour
--------------------------------------------

* After reusing a backend connection fails once, a fresh connection
  will be opened (2135_).

.. _2135: https://github.com/varnishcache/varnish-cache/pull/2135

Fixed bugs
----------

* Honor first_byte_timeout for recycled backend connections. (1772_)

* Limit backend connection retries to a single retry (2135_)

* H2: Move the req-specific PRIV pointers to struct req. (2268_)

* H2: Don't panic if we reembark with a request body (2305_)

* Clear the objcore attributes flags when (re)initializing an stv object. (2319_)

* H2: Fail streams with missing :method or :path. (2351_)

* H2: Enforce sequence requirement of header block frames. (2387_)

* H2: Hold the sess mutex when evaluating r2->cond. (2434_)

* Use the idle read timeout only on empty requests. (2492_)

* OH leak in http1_reembark. (2495_)

* Fix objcore reference count leak. (2502_)

* Close a race between backend probe and vcl.state=Cold by removing
  the be->vsc under backend mtx. (2505_)

* Fail gracefully if shard.backend() is called in housekeeping subs (2506_)

* Fix issue #1799 for keep. (2519_)

* oc->last_lru as float gives too little precision. (2527_)

* H2: Don't HTC_RxStuff with a non-reserved workspace. (2539_)

* Various optimizations of VSM. (2430_, 2470_, 2518_, 2535_, 2541_, 2545_, 2546_)

* Problems during late socket initialization performed by the Varnish
  child process can now be reported back to the management process with an
  error message. (2551_)

* Fail if ESI is attempted on partial (206) objects.

* Assert error in ban_mark_completed() - ban lurker edge case. (2556_)

* Accurate byte counters (2558_). See Logging / statistics above.

* H2: Fix reembark failure handling. (2563_ and 2592_)

* Working directory permissions insufficient when starting with
  umask 027. (2570_)

* Always use HTTP/1.1 on backend connections for pass & fetch. (2574_)

* EPIPE is a documented errno in tcp(7) on linux. (2582_)

* H2: Handle failed write(2) in h2_ou_session. (2607_)

.. _1772: https://github.com/varnishcache/varnish-cache/issues/1772
.. _2135: https://github.com/varnishcache/varnish-cache/pull/2135
.. _2268: https://github.com/varnishcache/varnish-cache/issues/2268
.. _2305: https://github.com/varnishcache/varnish-cache/issues/2305
.. _2319: https://github.com/varnishcache/varnish-cache/issues/2319
.. _2351: https://github.com/varnishcache/varnish-cache/issues/2351
.. _2387: https://github.com/varnishcache/varnish-cache/issues/2387
.. _2430: https://github.com/varnishcache/varnish-cache/issues/2430
.. _2434: https://github.com/varnishcache/varnish-cache/issues/2434
.. _2470: https://github.com/varnishcache/varnish-cache/issues/2470
.. _2492: https://github.com/varnishcache/varnish-cache/issues/2492
.. _2495: https://github.com/varnishcache/varnish-cache/issues/2495
.. _2502: https://github.com/varnishcache/varnish-cache/issues/2502
.. _2505: https://github.com/varnishcache/varnish-cache/issues/2505
.. _2506: https://github.com/varnishcache/varnish-cache/issues/2506
.. _2518: https://github.com/varnishcache/varnish-cache/issues/2518
.. _2519: https://github.com/varnishcache/varnish-cache/pull/2519
.. _2527: https://github.com/varnishcache/varnish-cache/issues/2527
.. _2535: https://github.com/varnishcache/varnish-cache/issues/2535
.. _2539: https://github.com/varnishcache/varnish-cache/issues/2539
.. _2541: https://github.com/varnishcache/varnish-cache/issues/2541
.. _2545: https://github.com/varnishcache/varnish-cache/pull/2545
.. _2546: https://github.com/varnishcache/varnish-cache/issues/2546
.. _2551: https://github.com/varnishcache/varnish-cache/issues/2551
.. _2554: https://github.com/varnishcache/varnish-cache/pull/2554
.. _2556: https://github.com/varnishcache/varnish-cache/issues/2556
.. _2558: https://github.com/varnishcache/varnish-cache/pull/2558
.. _2563: https://github.com/varnishcache/varnish-cache/issues/2563
.. _2570: https://github.com/varnishcache/varnish-cache/issues/2570
.. _2574: https://github.com/varnishcache/varnish-cache/issues/2574
.. _2582: https://github.com/varnishcache/varnish-cache/issues/2582
.. _2592: https://github.com/varnishcache/varnish-cache/issues/2592
.. _2607: https://github.com/varnishcache/varnish-cache/issues/2607

================================
Varnish Cache 5.2.1 (2017-11-14)
================================

Bugs fixed
----------

* 2429_ - Avoid buffer read overflow on vcl_backend_error and -sfile
* 2492_ - Use the idle read timeout only on empty requests.

.. _2429: https://github.com/varnishcache/varnish-cache/pull/2429
.. _2492: https://github.com/varnishcache/varnish-cache/issues/2492

================================
Varnish Cache 5.2.0 (2017-09-15)
================================

* The ``cli_buffer`` parameter has been deprecated (2382_)

.. _2382: https://github.com/varnishcache/varnish-cache/pull/2382

==================================
Varnish Cache 5.2-RC1 (2017-09-04)
==================================

Usage
-----

* The default for the the -i argument is now the hostname as returned
  by gethostname(3)

* Where possible (on platforms with setproctitle(3)), the -i argument
  rather than the -n argument is used for process names

* varnishd -f honors ``vcl_path`` (#2342)

* The ``MAIN.s_req`` statistic has been removed, as it was identical to
  ``MAIN.client_req``. VSM consumers should be changed to use the
  latter if necessary.

* A listen address can take a name in the -a argument. This name is used
  in the logs and later will possibly be available in VCL.

VCL
---

* VRT_purge fails a transaction if used outside of ``vcl_hit`` and
  ``vcl_miss`` (#2339)

* Added ``bereq.is_bgfetch`` which is true for background fetches.

* Added VMOD purge (#2404)

* Added VMOD blob (#2407)

C APIs (for vmod and utility authors)
-------------------------------------

* The VSM API for accessing the shared memory segment has been
  totally rewritten.  Things should be simpler and more general.

* VSC shared memory layout has changed and the VSC API updated
  to match it.  This paves the way for user defined VSC counters
  in VMODS and later possibly also in VCL.

* New vmod vtc for advanced varnishtest usage (#2276)

================================
Varnish Cache 5.1.3 (2017-08-02)
================================

Bugs fixed
----------

* 2379_ - Correctly handle bogusly large chunk sizes (VSV00001)

.. _2379: https://github.com/varnishcache/varnish-cache/issues/2379

================================
Varnish Cache 5.1.2 (2017-04-07)
================================

* Fix an endless loop in Backend Polling (#2295)

* Fix a Chunked bug in tight workspaces (#2207, #2275)

* Fix a bug relating to req.body when on waitinglist (#2266)

* Handle EPIPE on broken TCP connections (#2267)

* Work around the x86 arch's turbo-double FP format in parameter
  setup code. (#1875)

* Fix race related to backend probe with proxy header (#2278)

* Keep VCL temperature consistent between mgt/worker also when
  worker protests.

* A lot of HTTP/2 fixes.

================================
Varnish Cache 5.1.1 (2017-03-16)
================================

* Fix bug introduced by stubborn old bugger right before release
  5.1.0 was cut.

================================
Varnish Cache 5.1.0 (2017-03-15)
================================

* Added varnishd command-line options -I, -x and -?, and tightened
  restrictions on permitted combinations of options.

* More progress on support for HTTP/2.

* Add ``return(fail)`` to almost all VCL subroutines.

* Restored the old hit-for-pass, invoked with
  ``return(pass(DURATION))`` from
  ``vcl_backend_response``. hit-for-miss remains the default.  Added
  the cache_hitmiss stat, and cache_hitpass only counts the new/old
  hit-for-pass cases. Restored HitPass to the Varnish log, and added
  HitMiss. Added the HFP prefix to TTL log entries to log a
  hit-for-pass duration.

* Rolled back the fix for #1206. Client delivery decides solely whether
  to send a 304 client response, based on client request and response
  headers.

* Added vtest.sh.

* Added vxid as a lefthand side for VSL queries.

* Added the setenv and write_body commands for Varnish test cases (VTCs).
  err_shell is deprecated. Also added the operators -cliexpect, -match and
  -hdrlen, and -reason replaces -msg. Added the ${bad_backend} macro.

* varnishtest can be stopped with the TERM, INT and KILL signals, but
  not with HUP.

* The fallback director has now an extra, optional parameter to keep
  using the current backend until it falls sick.

* VMOD shared libraries are now copied to the workdir, to avoid problems
  when VMODs are updated via packaging systems.

* Bump the VRT version to 6.0.

* Export more symbols from libvarnishapi.so.

* The size of the VSL log is limited to 4G-1b, placing upper bounds on
  the -l option and the vsl_space and vsm_space parameters.

* Added parameters clock_step, thread_pool_reserve and ban_cutoff.

* Parameters vcl_dir and vmod_dir are deprecated, use vcl_path and
  vmod_path instead.

* All parameters are defined, even on platforms that don't support
  them.  An unsupported parameter is documented as such in
  param.show. Setting such a parameter is not an error, but has no
  effect.

* Clarified the interpretations of the + and - operators in VCL with
  operands of the various data types.

* DURATION types may be used in boolean contexts.

* INT, DURATION and REAL values can now be negative.

* Response codes 1000 or greater may now be set in VCL internally.
  resp.status is delivered modulo 1000 in client responses.

* IP addresses can be compared for equality in VCL.

* Introduce the STEVEDORE data type, and the objects storage.SNAME
  in VCL.  Added req.storage and beresp.storage; beresp.storage_hint
  is deprecated.

* Retired the umem stevedore.

* req.ttl is deprecated.

* Added std.getenv() and std.late_100_continue().

* The fetch_failed stat is incremented for any kind of fetch failure.

* Added the stats n_test_gunzip and bans_lurker_obj_killed_cutoff.

* Clarified the meanings of the %r, %{X}i and %{X}o formatters in
  varnishncsa.

Bugs fixed
----------

* 2251_ - varnishapi.pc and varnishconfdir
* 2250_ - vrt.h now depends on vdef.h making current vmod fail.
* 2249_ - "logexpect -wait" doesn't fail
* 2245_ - Varnish doesn't start, if use vmod (vmod_cache dir was permission denied)
* 2241_ - VSL fails to get hold of SHM
* 2233_ - Crash on "Assert error in WS_Assert(), cache/cache_ws.c line 59"
* 2227_ - -C flag broken in HEAD
* 2217_ - fix argument processing -C regression
* 2207_ - Assert error in V1L_Write()
* 2205_ - Strange bug when I set client.ip with another string
* 2203_ - unhandled SIGPIPE
* 2200_ - Assert error in vev_compact_pfd(), vev.c line 394
* 2197_ - ESI parser panic on malformed src URL
* 2190_ - varnishncsa: The %r formatter is NOT equivalent to "%m http://%{Host}i%U%q %H"
* 2186_ - Assert error in sml_iterator(), storage/storage_simple.c line 263
* 2184_ - Cannot subtract a negative number
* 2177_ - Clarify interactions between restarts and labels
* 2175_ - Backend leak between a top VCL and a label
* 2174_ - Cflags overhaul
* 2167_ - VCC will not parse a literal negative number where INT is expected
* 2155_ - vmodtool removes text following $Event from RST docs
* 2151_ - Health probes do not honor a backend's PROXY protocol setting
* 2142_ - ip comparison fails
* 2148_ - varnishncsa cannot decode Authorization header if the format is incorrect.
* 2143_ - Assert error in exp_inbox(), cache/cache_expire.c line 195
* 2134_ - Disable Nagle's
* 2129_ - stack overflow with >4 level esi
* 2128_ - SIGSEGV NULL Pointer in STV__iter()
* 2118_ - "varnishstat -f MAIN.sess_conn -1" produces empty output
* 2117_ - SES_Close() EBADF / Wait_Enter() wp->fd <= 0
* 2115_ - VSM temporary files are not always deleted
* 2110_ - [CLI] vcl.inline failures
* 2104_ - Assert error in VFP_Open(), cache/cache_fetch_proc.c line 139: Condition((vc->wrk->vsl) != 0) not true
* 2099_ - VCC BACKEND/HDR comparison produces duplicate gethdr_s definition
* 2096_ - H2 t2002 fail on arm64/arm32
* 2094_ - H2 t2000 fail on arm64/arm32
* 2078_ - VCL comparison doesn't fold STRING_LIST
* 2052_ - d12.vtc flaky when compiling with suncc
* 2042_ - Send a 304 response for a just-gone-stale hitpass object when appropriate
* 2041_ - Parent process should exit if it fails to start child
* 2035_ - varnishd stalls with two consecutive Range requests using HTTP persistent connections
* 2026_ - Add restart of poll in read_tmo
* 2021_ - vcc "used before defined" check
* 2017_ - "%r" field is wrong
* 2016_ - confusing vcc error when acl referenced before definition
* 2014_ - req.ttl: retire or document+vtc
* 2010_ - varnishadm CLI behaving weirdly
* 1991_ - Starting varnish on Linux with boot param ipv6.disable=1 fails
* 1988_ - Lost req.url gives misleading error
* 1914_ - set a custom storage for cache_req_body
* 1899_ - varnishadm vcl.inline is overly obscure
* 1874_ - clock-step related crash
* 1865_ - Panic accessing beresp.backend.ip in vcl_backend_error{}
* 1856_ - LostHeader setting req.url to an empty string
* 1834_ - WS_Assert(), cache/cache_ws.c line 59
* 1830_ - VSL API: "duplicate link" errors in request grouping when vsl_buffer is increased
* 1764_ - nuke_limit is not honored
* 1750_ - Fail more gracefully on -l >= 4GB
* 1704_ - fetch_failed not incremented

.. _2251: https://github.com/varnishcache/varnish-cache/issues/2251
.. _2250: https://github.com/varnishcache/varnish-cache/issues/2250
.. _2249: https://github.com/varnishcache/varnish-cache/issues/2249
.. _2245: https://github.com/varnishcache/varnish-cache/issues/2245
.. _2241: https://github.com/varnishcache/varnish-cache/issues/2241
.. _2233: https://github.com/varnishcache/varnish-cache/issues/2233
.. _2227: https://github.com/varnishcache/varnish-cache/issues/2227
.. _2217: https://github.com/varnishcache/varnish-cache/issues/2217
.. _2207: https://github.com/varnishcache/varnish-cache/issues/2207
.. _2205: https://github.com/varnishcache/varnish-cache/issues/2205
.. _2203: https://github.com/varnishcache/varnish-cache/issues/2203
.. _2200: https://github.com/varnishcache/varnish-cache/issues/2200
.. _2197: https://github.com/varnishcache/varnish-cache/issues/2197
.. _2190: https://github.com/varnishcache/varnish-cache/issues/2190
.. _2186: https://github.com/varnishcache/varnish-cache/issues/2186
.. _2184: https://github.com/varnishcache/varnish-cache/issues/2184
.. _2177: https://github.com/varnishcache/varnish-cache/issues/2177
.. _2175: https://github.com/varnishcache/varnish-cache/issues/2175
.. _2174: https://github.com/varnishcache/varnish-cache/issues/2174
.. _2167: https://github.com/varnishcache/varnish-cache/issues/2167
.. _2155: https://github.com/varnishcache/varnish-cache/issues/2155
.. _2151: https://github.com/varnishcache/varnish-cache/issues/2151
.. _2142: https://github.com/varnishcache/varnish-cache/issues/2142
.. _2148: https://github.com/varnishcache/varnish-cache/issues/2148
.. _2143: https://github.com/varnishcache/varnish-cache/issues/2143
.. _2134: https://github.com/varnishcache/varnish-cache/issues/2134
.. _2129: https://github.com/varnishcache/varnish-cache/issues/2129
.. _2128: https://github.com/varnishcache/varnish-cache/issues/2128
.. _2118: https://github.com/varnishcache/varnish-cache/issues/2118
.. _2117: https://github.com/varnishcache/varnish-cache/issues/2117
.. _2115: https://github.com/varnishcache/varnish-cache/issues/2115
.. _2110: https://github.com/varnishcache/varnish-cache/issues/2110
.. _2104: https://github.com/varnishcache/varnish-cache/issues/2104
.. _2099: https://github.com/varnishcache/varnish-cache/issues/2099
.. _2096: https://github.com/varnishcache/varnish-cache/issues/2096
.. _2094: https://github.com/varnishcache/varnish-cache/issues/2094
.. _2078: https://github.com/varnishcache/varnish-cache/issues/2078
.. _2052: https://github.com/varnishcache/varnish-cache/issues/2052
.. _2042: https://github.com/varnishcache/varnish-cache/issues/2042
.. _2041: https://github.com/varnishcache/varnish-cache/issues/2041
.. _2035: https://github.com/varnishcache/varnish-cache/issues/2035
.. _2026: https://github.com/varnishcache/varnish-cache/issues/2026
.. _2021: https://github.com/varnishcache/varnish-cache/issues/2021
.. _2017: https://github.com/varnishcache/varnish-cache/issues/2017
.. _2016: https://github.com/varnishcache/varnish-cache/issues/2016
.. _2014: https://github.com/varnishcache/varnish-cache/issues/2014
.. _2010: https://github.com/varnishcache/varnish-cache/issues/2010
.. _1991: https://github.com/varnishcache/varnish-cache/issues/1991
.. _1988: https://github.com/varnishcache/varnish-cache/issues/1988
.. _1914: https://github.com/varnishcache/varnish-cache/issues/1914
.. _1899: https://github.com/varnishcache/varnish-cache/issues/1899
.. _1874: https://github.com/varnishcache/varnish-cache/issues/1874
.. _1865: https://github.com/varnishcache/varnish-cache/issues/1865
.. _1856: https://github.com/varnishcache/varnish-cache/issues/1856
.. _1834: https://github.com/varnishcache/varnish-cache/issues/1834
.. _1830: https://github.com/varnishcache/varnish-cache/issues/1830
.. _1764: https://github.com/varnishcache/varnish-cache/issues/1764
.. _1750: https://github.com/varnishcache/varnish-cache/issues/1750
.. _1704: https://github.com/varnishcache/varnish-cache/issues/1704

================================
Varnish Cache 5.0.0 (2016-09-15)
================================

* Documentation updates, especially the what's new and upgrade sections.

* Via: header made by Varnish now says 5.0.

* VMOD VRT ABI level increased.

* [vcl] obj.(ttl|age|grace|keep) is now readable in vcl_deliver.

* Latest devicedetect.vcl imported from upstream.

* New system wide VCL directory: ``/usr/share/varnish/vcl/``

* std.integer() can now convert from REAL.

Bugs fixed
----------

* 2086_ - Ignore H2 upgrades if the feature is not enabled.
* 2054_ - Introduce new macros for out-of-tree VMODs
* 2022_ - varnishstat -1 -f field inclusion glob doesn't allow VBE backend fields
* 2008_ - Panic: Assert error in VBE_Delete()
* 1800_ - PRIV_TASK in vcl_init/fini

.. _2086: https://github.com/varnishcache/varnish-cache/issues/2086
.. _2054: https://github.com/varnishcache/varnish-cache/issues/2054
.. _2022: https://github.com/varnishcache/varnish-cache/issues/2022
.. _2008: https://github.com/varnishcache/varnish-cache/issues/2008
.. _1800: https://github.com/varnishcache/varnish-cache/issues/1800


======================================
Varnish Cache 5.0.0-beta1 (2016-09-09)
======================================

This is the first beta release of the upcoming 5.0 release.

The list of changes are numerous and will not be expanded on in detail.

The release notes contain more background information and are highly
recommended reading before using any of the new features.

Major items:

* VCL labels, allowing for per-vhost (or per-anything) separate VCL files.

* (Very!) experimental support for HTTP/2.

* Always send the request body to the backend, making possible to cache
  responses of POST, PATCH requests etc with appropriate custom VCL and/or
  VMODs.

* hit-for-pass is now actually hit-for-miss.

* new shard director for loadbalancing by consistent hashing

* ban lurker performance improvements

* access to obj.ttl, obj.age, obj.grace and obj.keep in vcl_deliver

News for Vmod Authors
---------------------

* workspace and PRIV_TASK for vcl cli events (init/fini methods)

* PRIV_* now also work for object methods with unchanged scope.

================================
Varnish Cache 4.1.9 (2017-11-14)
================================

Changes since 4.1.8:

* Added ``bereq.is_bgfetch`` which is true for background fetches.
* Add the vtc feature ignore_unknown_macro.
* Expose to VCL whether or not a fetch is a background fetch (bgfetch)
* Ignore req.ttl when keeping track of expired objects (see 2422_)
* Move a cli buffer to VSB (from stack).
* Use a separate stack for signals.

.. _2422: https://github.com/varnishcache/varnish-cache/pull/2422

Bugs fixed
----------

* 2337_ and 2366_ - Both Upgrade and Connection headers are needed for
  WebSocket now
* 2372_ - Fix problem with purging and the n_obj_purged counter
* 2373_ - VSC n_vcl, n_vcl_avail, n_vcl_discard are gauge
* 2380_ - Correct regexp in examples.
* 2390_ - Straighten locking wrt vcl_active
* 2429_ - Avoid buffer read overflow on vcl_backend_error and -sfile
* 2492_ - Use the idle read timeout only on empty requests

.. _2337: https://github.com/varnishcache/varnish-cache/issues/2337
.. _2366: https://github.com/varnishcache/varnish-cache/issues/2366
.. _2372: https://github.com/varnishcache/varnish-cache/pull/2372
.. _2373: https://github.com/varnishcache/varnish-cache/issues/2373
.. _2380: https://github.com/varnishcache/varnish-cache/issues/2380
.. _2390: https://github.com/varnishcache/varnish-cache/issues/2390
.. _2429: https://github.com/varnishcache/varnish-cache/pull/2429
.. _2492: https://github.com/varnishcache/varnish-cache/issues/2492

================================
Varnish Cache 4.1.8 (2017-08-02)
================================

Changes since 4.1.7:

* Update in the documentation of timestamps

Bugs fixed
----------

* 2379_ - Correctly handle bogusly large chunk sizes (VSV00001)

.. _2379: https://github.com/varnishcache/varnish-cache/issues/2379

================================
Varnish Cache 4.1.7 (2017-06-28)
================================

Changes since 4.1.7-beta1:

* Add extra locking to protect the pools list and refcounts
* Don't panic on a null ban

Bugs fixed
----------

* 2321_ - Prevent storage backends name collisions

.. _2321: https://github.com/varnishcache/varnish-cache/issues/2321

======================================
Varnish Cache 4.1.7-beta1 (2017-06-15)
======================================

Changes since 4.1.6:

* Add -vsl_catchup to varnishtest
* Add record-prefix support to varnishncsa

Bugs fixed
----------
* 1764_ - Correctly honor nuke_limit parameter
* 2022_ - varnishstat -1 -f field inclusion glob doesn't allow VBE
  backend fields
* 2069_ - Health probes fail when HTTP response does not contain
  reason phrase
* 2118_ - "varnishstat -f MAIN.sess_conn -1" produces empty output
* 2219_ - Remember to reset workspace
* 2320_ - Rework and fix varnishstat counter filtering
* 2329_ - Docfix: Only root can jail

.. _1764: https://github.com/varnishcache/varnish-cache/issues/1764
.. _2022: https://github.com/varnishcache/varnish-cache/issues/2022
.. _2069: https://github.com/varnishcache/varnish-cache/issues/2069
.. _2118: https://github.com/varnishcache/varnish-cache/issues/2118
.. _2219: https://github.com/varnishcache/varnish-cache/issues/2219
.. _2320: https://github.com/varnishcache/varnish-cache/issues/2320
.. _2329: https://github.com/varnishcache/varnish-cache/issues/2329

================================
Varnish Cache 4.1.6 (2017-04-26)
================================

* Introduce a vxid left hand side for VSL queries. This allows
  matching on records matching a known vxid.
* Environment variables are now available in the stdandard VMOD;
  std.getenv()
* Add setenv command to varnishtest


Bugs fixed
----------
* 2200_ - Dramatically simplify VEV, fix assert in vev.c
* 2216_ - Make sure Age is always less than max-age
* 2233_ - Correct check when parsing the query string
* 2241_ - VSL fails to get hold of SHM
* 2270_ - Newly loaded auto VCLs don't get their go_cold timer set
* 2273_ - Master cooling problem
* 2275_ - If the client workspace is almost, but not quite exhausted, we may
  not be able to get enough iovec's to do Chunked transmission.
* 2295_ - Spinning loop in VBE_Poll causes master to kill child on
  CLI timeout
* 2301_ - Don't attempt to check if varnishd is still running if we have
  already failed.
* 2313_ - Cannot link to varnishapi, symbols missing

.. _2200: https://github.com/varnishcache/varnish-cache/issues/2200
.. _2216: https://github.com/varnishcache/varnish-cache/pull/2216
.. _2233: https://github.com/varnishcache/varnish-cache/issues/2233
.. _2241: https://github.com/varnishcache/varnish-cache/issues/2241
.. _2270: https://github.com/varnishcache/varnish-cache/issues/2270
.. _2273: https://github.com/varnishcache/varnish-cache/pull/2273
.. _2275: https://github.com/varnishcache/varnish-cache/issues/2275
.. _2295: https://github.com/varnishcache/varnish-cache/issues/2295
.. _2301: https://github.com/varnishcache/varnish-cache/issues/2301
.. _2313: https://github.com/varnishcache/varnish-cache/issues/2313

================================
Varnish Cache 4.1.5 (2017-02-09)
================================

* No code changes since 4.1.5-beta2.

======================================
Varnish Cache 4.1.5-beta2 (2017-02-08)
======================================

* Update devicedetect.vcl

Bugs fixed
----------

* 1704_ - Reverted the docfix and made the fetch_failed counter do
  what the documentation says it should do
* 1865_ - Panic accessing beresp.backend.ip in vcl_backend_error
* 2167_ - VCC will not parse a literal negative number where INT is
  expected
* 2184_ - Cannot subtract a negative number

.. _1704: https://github.com/varnishcache/varnish-cache/issues/1704
.. _1865: https://github.com/varnishcache/varnish-cache/issues/1865
.. _2167: https://github.com/varnishcache/varnish-cache/issues/2167
.. _2184: https://github.com/varnishcache/varnish-cache/issues/2184

======================================
Varnish Cache 4.1.5-beta1 (2017-02-02)
======================================

Bugs fixed
----------

* 1704_ - (docfix) Clarify description of fetch_failed counter
* 1834_ - Panic in workspace exhaustion conditions
* 2106_ - 4.1.3: Varnish crashes with "Assert error in CNT_Request(),
  cache/cache_req_fsm.c line 820"
* 2134_ - Disable Nagle's
* 2148_ - varnishncsa cannot decode Authorization header if the
  format is incorrect.
* 2168_ - Compare 'bereq.backend' / 'req.backend_hint'
  myDirector.backend() does not work
* 2178_ - 4.1 branch does not compile on FreeBSD
* 2188_ - Fix vsm_free (never incremented)
* 2190_ - (docfix)varnishncsa: The %r formatter is NOT equivalent to...
* 2197_ - ESI parser panic on malformed src URL

.. _1704: https://github.com/varnishcache/varnish-cache/issues/1704
.. _1834: https://github.com/varnishcache/varnish-cache/issues/1834
.. _2106: https://github.com/varnishcache/varnish-cache/issues/2106
.. _2134: https://github.com/varnishcache/varnish-cache/issues/2134
.. _2148: https://github.com/varnishcache/varnish-cache/issues/2148
.. _2168: https://github.com/varnishcache/varnish-cache/issues/2168
.. _2178: https://github.com/varnishcache/varnish-cache/issues/2178
.. _2188: https://github.com/varnishcache/varnish-cache/pull/2188
.. _2190: https://github.com/varnishcache/varnish-cache/issues/2190
.. _2197: https://github.com/varnishcache/varnish-cache/issues/2197

================================
Varnish Cache 4.1.4 (2016-12-01)
================================

Bugs fixed
----------

* 2035_ - varnishd stalls with two consecutive Range requests using
  HTTP persistent connections

.. _2035: https://github.com/varnishcache/varnish-cache/issues/2035

======================================
Varnish Cache 4.1.4-beta3 (2016-11-24)
======================================

* Include the current time of the panic in the panic output
* Keep a reserve of idle threads for vital tasks

Bugs fixed
----------

* 1874_ - clock-step related crash
* 1889_ - (docfix) What does -p flag for backend.list command means
* 2115_ - VSM temporary files are not always deleted
* 2129_ - (docfix) stack overflow with >4 level esi

.. _1874: https://github.com/varnishcache/varnish-cache/issues/1874
.. _1889: https://github.com/varnishcache/varnish-cache/issues/1889
.. _2115: https://github.com/varnishcache/varnish-cache/issues/2115
.. _2129: https://github.com/varnishcache/varnish-cache/issues/2129

======================================
Varnish Cache 4.1.4-beta2 (2016-10-13)
======================================

Bugs fixed
----------

* 1830_ - VSL API: "duplicate link" errors in request grouping when
  vsl_buffer is increased
* 2010_ - varnishadm CLI behaving weirdly
* 2017_ - varnishncsa docfix: "%r" field is wrong
* 2107_ - (docfix) HEAD requestes changed to GET

.. _1830: https://github.com/varnishcache/varnish-cache/issues/1830
.. _2010: https://github.com/varnishcache/varnish-cache/issues/2010
.. _2017: https://github.com/varnishcache/varnish-cache/issues/2017
.. _2107: https://github.com/varnishcache/varnish-cache/issues/2107

======================================
Varnish Cache 4.1.4-beta1 (2016-09-14)
======================================

* [varnishhist] Various improvements
* [varnishtest] A `cmd` feature for custom shell-based checks
* Documentation improvements (do_stream, sess_herd, timeout_linger, thread_pools)
* [varnishtop] Documented behavior when both -p and -1 are specified

Bugs fixed
----------

* 2027_ - Racy backend selection
* 2024_ - panic vmod_rr_resolve() round_robin.c line 75 (be) != NULL
* 2011_ - VBE.*.conn (concurrent connections to backend) not working as expected
* 2008_ - Assert error in VBE_Delete()
* 2007_ - Update documentation part about CLI/management port authentication parameter
* 1881_ - std.cache_req_body() w/ return(pipe) is broken

.. _2027: https://github.com/varnishcache/varnish-cache/issues/2027
.. _2024: https://github.com/varnishcache/varnish-cache/issues/2024
.. _2011: https://github.com/varnishcache/varnish-cache/issues/2011
.. _2008: https://github.com/varnishcache/varnish-cache/issues/2008
.. _2007: https://github.com/varnishcache/varnish-cache/issues/2007
.. _1881: https://github.com/varnishcache/varnish-cache/issues/1881

================================
Varnish Cache 4.1.3 (2016-07-06)
================================

* Be stricter when parsing request headers to harden against smuggling attacks.

======================================
Varnish Cache 4.1.3-beta2 (2016-06-28)
======================================

* New parameter `vsm_free_cooldown`. Specifies how long freed VSM
  memory (shared log) will be kept around before actually being freed.

* varnishncsa now accepts `-L` argument to configure the limit on incomplete
  transactions kept. (Issue 1994_)

Bugs fixed
----------

* 1984_ - Make the counter vsm_cooling act according to spec
* 1963_ - Avoid abort when changing to a VCL name which is a path
* 1933_ - Don't trust dlopen refcounting

.. _1994: https://github.com/varnishcache/varnish-cache/issues/1994
.. _1984: https://github.com/varnishcache/varnish-cache/issues/1984
.. _1963: https://github.com/varnishcache/varnish-cache/issues/1963
.. _1933: https://github.com/varnishcache/varnish-cache/issues/1933

======================================
Varnish Cache 4.1.3-beta1 (2016-06-15)
======================================

* varnishncsa can now access and log backend requests. (PR #1905)

* [varnishncsa] New output formatters %{Varnish:vxid}x and %{VSL:Tag}x.

* [varnishlog] Added log tag BackendStart on backend transactions.

* On SmartOS, use ports instead of epoll by default.

* Add support for TCP Fast Open where available. Disabled by default.

* [varnishtest] New syncronization primitive barriers added, improving
  coordination when test cases call external programs.

.. _1905: https://github.com/varnishcache/varnish-cache/pull/1905

Bugs fixed
----------

* 1971_ - Add missing Wait_HeapDelete
* 1967_ - [ncsa] Remove implicit line feed when using formatfile
* 1955_ - 4.1.x sometimes duplicates Age and Accept-Ranges headers
* 1954_ - Correctly handle HTTP/1.1 EOF response
* 1953_ - Deal with fetch failures in ved_stripgzip
* 1931_ - Allow VCL set Last-Modified to be used for I-M-S processing
* 1928_ - req->task members must be set in case we get onto the waitinglist
* 1924_ - Make std.log() and std.syslog() work from vcl_{init,fini}
* 1919_ - Avoid ban lurker panic with empty olist
* 1918_ - Correctly handle EOF responses with HTTP/1.1
* 1912_ - Fix (insignificant) memory leak with mal-formed ESI directives.
* 1904_ - Release memory instead of crashing on malformed ESI
* 1885_ - [vmodtool] Method names should start with a period
* 1879_ - Correct handling of duplicate headers on IMS header merge
* 1878_ - Fix a ESI+gzip corner case which had escaped notice until now
* 1873_ - Check for overrun before looking at the next vsm record
* 1871_ - Missing error handling code in V1F_Setup_Fetch
* 1869_ - Remove temporary directory iff called with -C
* 1883_ - Only accept C identifiers as acls
* 1855_ - Truncate output if it's wider than 12 chars
* 1806_ - One minute delay on return (pipe) and a POST-Request
* 1725_ - Revive the backend_conn counter

.. _1971: https://github.com/varnishcache/varnish-cache/issues/1971
.. _1967: https://github.com/varnishcache/varnish-cache/issues/1967
.. _1955: https://github.com/varnishcache/varnish-cache/issues/1955
.. _1954: https://github.com/varnishcache/varnish-cache/issues/1954
.. _1953: https://github.com/varnishcache/varnish-cache/issues/1953
.. _1931: https://github.com/varnishcache/varnish-cache/issues/1931
.. _1928: https://github.com/varnishcache/varnish-cache/issues/1928
.. _1924: https://github.com/varnishcache/varnish-cache/issues/1924
.. _1919: https://github.com/varnishcache/varnish-cache/issues/1919
.. _1918: https://github.com/varnishcache/varnish-cache/issues/1918
.. _1912: https://github.com/varnishcache/varnish-cache/issues/1912
.. _1904: https://github.com/varnishcache/varnish-cache/issues/1904
.. _1885: https://github.com/varnishcache/varnish-cache/issues/1885
.. _1883: https://github.com/varnishcache/varnish-cache/issues/1883
.. _1879: https://github.com/varnishcache/varnish-cache/issues/1879
.. _1878: https://github.com/varnishcache/varnish-cache/issues/1878
.. _1873: https://github.com/varnishcache/varnish-cache/issues/1873
.. _1871: https://github.com/varnishcache/varnish-cache/issues/1871
.. _1869: https://github.com/varnishcache/varnish-cache/issues/1869
.. _1855: https://github.com/varnishcache/varnish-cache/issues/1855
.. _1806: https://github.com/varnishcache/varnish-cache/issues/1806
.. _1725: https://github.com/varnishcache/varnish-cache/issues/1725


================================
Varnish Cache 4.1.2 (2016-03-04)
================================

* [vmods] vmodtool improvements for multiple VMODs in a single directory.

Bugs fixed
----------

* 1860_ - ESI-related memory leaks
* 1863_ - Don't reset the oc->ban pointer from BAN_CheckObject
* 1864_ - Avoid panic if the lurker is working on a ban to be checked.

.. _1860: https://www.varnish-cache.org/trac/ticket/1860
.. _1863: https://www.varnish-cache.org/trac/ticket/1863
.. _1864: https://www.varnish-cache.org/trac/ticket/1864


======================================
Varnish Cache 4.1.2-beta2 (2016-02-25)
======================================

* [vmods] Passing VCL ACL to a VMOD is now possible.

* [vmods] VRT_MINOR_VERSION increase due to new function: VRT_acl_match()

* Some test case stabilization fixes and minor documentation updates.

* Improved handling of workspace exhaustion when fetching objects.

Bugs fixed
----------

* 1858_ - Hit-for-pass objects are not IMS candidates

.. _1858: https://www.varnish-cache.org/trac/ticket/1858

======================================
Varnish Cache 4.1.2-beta1 (2016-02-17)
======================================

* Be stricter when parsing a HTTP request to avoid potential
  HTTP smuggling attacks against vulnerable backends.

* Some fixes to minor/trivial issues found with clang AddressSanitizer.

* Arithmetic on REAL data type in VCL is now possible.

* vmodtool.py improvements to allow VMODs for 4.0 and 4.1 to share a source tree.

* Off-by-one in WS_Reset() fixed.

* "https_scheme" parameter added. Enables graceful handling of compound
  request URLs with HTTPS scheme. (Bug 1847_)

Bugs fixed
----------

* 1739_ - Workspace overflow handling in VFP_Push()
* 1837_ - Error compiling VCL if probe is referenced before it is defined
* 1841_ - Replace alien FD's with /dev/null rather than just closing them
* 1843_ - Fail HTTP/1.0 POST and PUT requests without Content-Length
* 1844_ - Correct ENUM handling in object constructors
* 1851_ - Varnish 4.1.1 fails to build on i386
* 1852_ - Add a missing VDP flush operation after ESI:includes.
* 1857_ - Fix timeout calculation for session herding.

.. _1739: https://www.varnish-cache.org/trac/ticket/1739
.. _1837: https://www.varnish-cache.org/trac/ticket/1837
.. _1841: https://www.varnish-cache.org/trac/ticket/1841
.. _1843: https://www.varnish-cache.org/trac/ticket/1843
.. _1844: https://www.varnish-cache.org/trac/ticket/1844
.. _1851: https://www.varnish-cache.org/trac/ticket/1851
.. _1852: https://www.varnish-cache.org/trac/ticket/1852
.. _1857: https://www.varnish-cache.org/trac/ticket/1857
.. _1847: https://www.varnish-cache.org/trac/ticket/1847


================================
Varnish Cache 4.1.1 (2016-01-28)
================================

* No code changes since 4.1.1-beta2.


======================================
Varnish Cache 4.1.1-beta2 (2016-01-22)
======================================

* Improvements to VCL temperature handling added. This opens for reliably
  deny warming a cooling VCL from a VMOD.

Bugs fixed
----------

* 1802_ - Segfault after VCL change
* 1825_ - Cannot Start Varnish After Just Restarting The Service
* 1842_ - Handle missing waiting list gracefully.
* 1845_ - Handle whitespace after floats in test fields

.. _1802: https://www.varnish-cache.org/trac/ticket/1802
.. _1825: https://www.varnish-cache.org/trac/ticket/1825
.. _1842: https://www.varnish-cache.org/trac/ticket/1842
.. _1845: https://www.varnish-cache.org/trac/ticket/1845


======================================
Varnish Cache 4.1.1-beta1 (2016-01-15)
======================================

- Format of "ban.list" has changed slightly.
- [varnishncsa] -w is now required when running daemonized.
- [varnishncsa] Log format can now be read from file.
- Port fields extracted from PROXY1 header now work as expected.
- New VCL state "busy" introduced (mostly for VMOD writers).
- Last traces of varnishreplay removed.
- If-Modified-Since is now ignored if we have If-None-Match.
- Zero Content-Length is no longer sent on 304 responses.
- vcl_dir and vmod_dir now accept a colon separated list of directories.
- Nested includes starting with "./" are relative to the including
  VCL file now.


Bugs fixed
----------

- 1796_ - Don't attempt to allocate a V1L from the workspace if it is overflowed.
- 1794_ - Fail if multiple -a arguments return the same suckaddr.
- 1763_ - Restart epoll_wait on EINTR error
- 1788_ - ObjIter has terrible performance profile when busyobj != NULL
- 1798_ - Varnish requests painfully slow with large files
- 1816_ - Use a weak comparison function for If-None-Match
- 1818_ - Allow grace-hits on hit-for-pass objects, [..]
- 1821_ - Always slim private & pass objects after delivery.
- 1823_ - Rush the objheader if there is a waiting list when it is deref'ed.
- 1826_ - Ignore 0 Content-Lengths in 204 responses
- 1813_ - Fail if multiple -a arguments return the same suckaddr.
- 1810_ - Improve handling of HTTP/1.0 clients
- 1807_ - Return 500 if we cannot decode the stored object into the resp.*
- 1804_ - Log proxy related messages on the session, not on the request.
- 1801_ - Relax IP constant parsing

.. _1796: https://www.varnish-cache.org/trac/ticket/1796
.. _1794: https://www.varnish-cache.org/trac/ticket/1794
.. _1763: https://www.varnish-cache.org/trac/ticket/1763
.. _1788: https://www.varnish-cache.org/trac/ticket/1788
.. _1798: https://www.varnish-cache.org/trac/ticket/1798
.. _1816: https://www.varnish-cache.org/trac/ticket/1816
.. _1818: https://www.varnish-cache.org/trac/ticket/1818
.. _1821: https://www.varnish-cache.org/trac/ticket/1821
.. _1823: https://www.varnish-cache.org/trac/ticket/1823
.. _1826: https://www.varnish-cache.org/trac/ticket/1826
.. _1813: https://www.varnish-cache.org/trac/ticket/1813
.. _1810: https://www.varnish-cache.org/trac/ticket/1810
.. _1807: https://www.varnish-cache.org/trac/ticket/1807
.. _1804: https://www.varnish-cache.org/trac/ticket/1804
.. _1801: https://www.varnish-cache.org/trac/ticket/1801


================================
Varnish Cache 4.1.0 (2015-09-30)
================================

- Documentation updates.
- Stabilization fixes on testcase p00005.vtc.
- Avoid compiler warning in zlib.
- Bug 1792_: Avoid using fallocate() with -sfile on non-EXT4.

.. _1792: https://www.varnish-cache.org/trac/ticket/1792


======================================
Varnish Cache 4.1.0-beta1 (2015-09-11)
======================================

- Redhat packaging files are now separate from the normal tree.
- Client workspace overflow should now result in a 500 response
  instead of panic.
- [varnishstat] -w option has been retired.
- libvarnishapi release number is increased.
- Body bytes sent on ESI subrequests with gzip are now counted correctly.
- [vmod-std] Data type conversion functions now take additional fallback argument.

Bugs fixed
----------

- 1777_ - Disable speculative Range handling on streaming transactions.
- 1778_ - [varnishstat] Cast to integer to prevent negative values messing the statistics
- 1781_ - Propagate gzip CRC upwards from nested ESI includes.
- 1783_ - Align code with RFC7230 section 3.3.3 which allows POST without a body.

.. _1777: https://www.varnish-cache.org/trac/ticket/1777
.. _1778: https://www.varnish-cache.org/trac/ticket/1778
.. _1781: https://www.varnish-cache.org/trac/ticket/1781
.. _1783: https://www.varnish-cache.org/trac/ticket/1783


====================================
Varnish Cache 4.1.0-tp1 (2015-07-08)
====================================

Changes between 4.0 and 4.1 are numerous. Please read the upgrade
section in the documentation for a general overview.


============================================
Changes from 4.0.3-rc3 to 4.0.3 (2015-02-17)
============================================

* No changes.

================================================
Changes from 4.0.3-rc2 to 4.0.3-rc3 (2015-02-11)
================================================

- Superseded objects are now expired immediately.

Bugs fixed
----------

- 1462_ - Use first/last log entry in varnishncsa.
- 1539_ - Avoid panic when expiry thread modifies a candidate object.
- 1637_ - Fail the fetch processing if the vep callback failed.
- 1665_ - Be more accurate when computing client RX_TIMEOUT.
- 1672_ - Do not panic on unsolicited 304 response to non-200 bereq.

.. _1462: https://www.varnish-cache.org/trac/ticket/1462
.. _1539: https://www.varnish-cache.org/trac/ticket/1539
.. _1637: https://www.varnish-cache.org/trac/ticket/1637
.. _1665: https://www.varnish-cache.org/trac/ticket/1665
.. _1672: https://www.varnish-cache.org/trac/ticket/1672


================================================
Changes from 4.0.3-rc1 to 4.0.3-rc2 (2015-01-28)
================================================

- Assorted documentation updates.

Bugs fixed
----------

- 1479_ - Fix out-of-tree builds.
- 1566_ - Escape VCL string question marks.
- 1616_ - Correct header file placement.
- 1620_ - Fail miss properly if out of backend threads. (Also 1621_)
- 1628_ - Avoid dereferencing null in VBO_DerefBusyObj().
- 1629_ - Ditch rest of waiting list on failure to reschedule.
- 1660_ - Don't attempt range delivery on a synth response

.. _1479: https://www.varnish-cache.org/trac/ticket/1479
.. _1566: https://www.varnish-cache.org/trac/ticket/1578
.. _1616: https://www.varnish-cache.org/trac/ticket/1616
.. _1620: https://www.varnish-cache.org/trac/ticket/1620
.. _1621: https://www.varnish-cache.org/trac/ticket/1621
.. _1628: https://www.varnish-cache.org/trac/ticket/1628
.. _1629: https://www.varnish-cache.org/trac/ticket/1629
.. _1660: https://www.varnish-cache.org/trac/ticket/1660


============================================
Changes from 4.0.2 to 4.0.3-rc1 (2015-01-15)
============================================

- Support older autoconf (< 2.63b) (el5)
- A lot of minor documentation fixes.
- bereq.uncacheable is now read-only.
- obj.uncacheable is now readable in vcl_deliver.
- [varnishadm] Prefer exact matches for backend.set_healthy. Bug 1349_.
- Hard-coded -sfile default size is removed.
- [packaging] EL6 packages are once again built with -O2.
- [parameter] fetch_chunksize default is reduced to 16KB. (from 128KB)
- Added std.time() which converts strings to VCL_TIME.
- [packaging] packages now Provide strictABI (gitref) and ABI (VRT major/minor) for VMOD use.

Bugs fixed
----------

* 1378_ - Properly escape non-printable characters in varnishncsa.
* 1596_ - Delay HSH_Complete() until the storage sanity functions has finished.
* 1506_ - Keep Content-Length from backend if we can.
* 1602_ - Fix a cornercase related to empty pass objects.
* 1607_ - Don't leak reqs on failure to revive from waitinglist.
* 1610_ - Update forgotten varnishlog example to 4.0 syntax.
* 1612_ - Fix a cornercase related to empty pass objects.
* 1623_ - Fix varnishhist -d segfault.
* 1636_ - Outdated paragraph in Vary: documentation
* 1638_ - Fix panic when retrying a failed backend fetch.
* 1639_ - Restore the default SIGSEGV handler during pan_ic
* 1647_ - Relax an assertion for the IMS update candidate object.
* 1648_ - Avoid partial IMS updates to replace old object.
* 1650_ - Collapse multiple X-Forwarded-For headers

.. _1349: https://www.varnish-cache.org/trac/ticket/1349
.. _1378: https://www.varnish-cache.org/trac/ticket/1378
.. _1596: https://www.varnish-cache.org/trac/ticket/1596
.. _1506: https://www.varnish-cache.org/trac/ticket/1506
.. _1602: https://www.varnish-cache.org/trac/ticket/1602
.. _1607: https://www.varnish-cache.org/trac/ticket/1607
.. _1610: https://www.varnish-cache.org/trac/ticket/1610
.. _1612: https://www.varnish-cache.org/trac/ticket/1612
.. _1623: https://www.varnish-cache.org/trac/ticket/1623
.. _1636: https://www.varnish-cache.org/trac/ticket/1636
.. _1638: https://www.varnish-cache.org/trac/ticket/1638
.. _1639: https://www.varnish-cache.org/trac/ticket/1639
.. _1647: https://www.varnish-cache.org/trac/ticket/1647
.. _1648: https://www.varnish-cache.org/trac/ticket/1648
.. _1650: https://www.varnish-cache.org/trac/ticket/1650


============================================
Changes from 4.0.2-rc1 to 4.0.2 (2014-10-08)
============================================

New since 4.0.2-rc1:

- [varnishlog] -k argument is back. (exit after n records)
- [varnishadm] vcl.show is now listed in help.


============================================
Changes from 4.0.1 to 4.0.2-rc1 (2014-09-23)
============================================

New since 4.0.1:

- [libvmod-std] New function strstr() for matching substrings.
- server.(hostname|identity) is now available in all VCL functions.
- VCL variable type BYTES was added.
- `workspace_client` default is now 9k.
- [varnishstat] Update interval can now be subsecond.
- Document that reloading VCL does not reload a VMOD.
- Guru meditation page is now valid HTML5.
- [varnishstat] hitrate calculation is back.
- New parameter `group_cc` adds a GID to the grouplist of
  VCL compiler sandbox.
- Parameter shm_reclen is now an alias for vsl_reclen.
- Workspace overflows are now handled with a 500 client response.
- VCL variable type added: HTTP, representing a HTTP header set.
- It is now possible to return(synth) from vcl_deliver.
- [varnishadm] vcl.show now has a -v option that output the
  complete set of VCL and included VCL files.
- RHEL7 packaging (systemd) was added.
- [libvmod-std] querysort() fixed parameter limit has been lifted.
- Fix small memory leak in ESI parser.
- Fix unreported race/assert in V1D_Deliver().

Bugs fixed
----------

* 1553_ - Fully reset workspace (incl. Vary state) before reusing it.
* 1551_ - Handle workspace exhaustion during purge.
* 1591_ - Group entries correctly in varnishtop.
* 1592_ - Bail out on workspace exhaustion in VRT_IP_string.
* 1538_ - Relax VMOD ABI check for release branches.
* 1584_ - Don't log garbage/non-HTTP requests. [varnishncsa]
* 1407_ - Don't rename VSM file until child has started.
* 1466_ - Don't leak request structs on restart after waitinglist.
* 1580_ - Output warning if started without -b and -f. [varnishd]
* 1583_ - Abort on fatal sandbox errors on Solaris. (Related: 1572_)
* 1585_ - Handle fatal sandbox errors.
* 1572_ - Exit codes have been cleaned up.
* 1569_ - Order of symbols should not influence compilation result.
* 1579_ - Clean up type inference in VCL.
* 1578_ - Don't count Age twice when computing new object TTL.
* 1574_ - std.syslog() logged empty strings.
* 1555_ - autoconf editline/readline build issue.
* 1568_ - Skip NULL arguments when hashing.
* 1567_ - Compile on systems without SO_SNDTIMEO/SO_RCVTIMEO.
* 1512_ - Changes to bereq are lost between v_b_r and v_b_f.
* 1563_ - Increase varnishtest read timeout.
* 1561_ - Never call a VDP with zero length unless done.
* 1562_ - Fail correctly when rereading a failed client request body.
* 1521_ - VCL compilation fails on OSX x86_64.
* 1547_ - Panic when increasing shm_reclen.
* 1503_ - Document return(retry).
* 1581_ - Don't log duplicate Begin records to shmlog.
* 1588_ - Correct timestamps on pipelined requests.
* 1575_ - Use all director backends when looking for a healthy one.
* 1577_ - Read the full request body if shunted to synth.
* 1532_ - Use correct VCL representation of reals.
* 1531_ - Work around libedit bug in varnishadm.

.. _1553: https://www.varnish-cache.org/trac/ticket/1553
.. _1551: https://www.varnish-cache.org/trac/ticket/1551
.. _1591: https://www.varnish-cache.org/trac/ticket/1591
.. _1592: https://www.varnish-cache.org/trac/ticket/1592
.. _1538: https://www.varnish-cache.org/trac/ticket/1538
.. _1584: https://www.varnish-cache.org/trac/ticket/1584
.. _1407: https://www.varnish-cache.org/trac/ticket/1407
.. _1466: https://www.varnish-cache.org/trac/ticket/1466
.. _1580: https://www.varnish-cache.org/trac/ticket/1580
.. _1583: https://www.varnish-cache.org/trac/ticket/1583
.. _1585: https://www.varnish-cache.org/trac/ticket/1585
.. _1572: https://www.varnish-cache.org/trac/ticket/1572
.. _1569: https://www.varnish-cache.org/trac/ticket/1569
.. _1579: https://www.varnish-cache.org/trac/ticket/1579
.. _1578: https://www.varnish-cache.org/trac/ticket/1578
.. _1574: https://www.varnish-cache.org/trac/ticket/1574
.. _1555: https://www.varnish-cache.org/trac/ticket/1555
.. _1568: https://www.varnish-cache.org/trac/ticket/1568
.. _1567: https://www.varnish-cache.org/trac/ticket/1567
.. _1512: https://www.varnish-cache.org/trac/ticket/1512
.. _1563: https://www.varnish-cache.org/trac/ticket/1563
.. _1561: https://www.varnish-cache.org/trac/ticket/1561
.. _1562: https://www.varnish-cache.org/trac/ticket/1562
.. _1521: https://www.varnish-cache.org/trac/ticket/1521
.. _1547: https://www.varnish-cache.org/trac/ticket/1547
.. _1503: https://www.varnish-cache.org/trac/ticket/1503
.. _1581: https://www.varnish-cache.org/trac/ticket/1581
.. _1588: https://www.varnish-cache.org/trac/ticket/1588
.. _1575: https://www.varnish-cache.org/trac/ticket/1575
.. _1577: https://www.varnish-cache.org/trac/ticket/1577
.. _1532: https://www.varnish-cache.org/trac/ticket/1532
.. _1531: https://www.varnish-cache.org/trac/ticket/1531


========================================
Changes from 4.0.0 to 4.0.1 (2014-06-24)
========================================

New since 4.0.0:

- New functions in vmod_std: real2time, time2integer, time2real, real.
- Chunked requests are now supported. (pass)
- Add std.querysort() that sorts GET query arguments. (from libvmod-boltsort)
- Varnish will no longer reply with "200 Not Modified".
- Backend IMS is now only attempted when last status was 200.
- Packaging now uses find-provides instead of find-requires. [redhat]
- Two new counters: n_purges and n_obj_purged.
- Core size can now be set from /etc/sysconfig/varnish [redhat]
- Via header set is now RFC compliant.
- Removed "purge" keyword in VCL. Use return(purge) instead.
- fallback director is now documented.
- %D format flag in varnishncsa is now truncated to an integer value.
- persistent storage backend is now deprecated.
  https://www.varnish-cache.org/docs/trunk/phk/persistent.html
- Added format flags %I (total bytes received) and %O (total bytes sent) for
  varnishncsa.
- python-docutils >= 0.6 is now required.
- Support year (y) as a duration in VCL.
- VMOD ABI requirements are relaxed, a VMOD no longer have to be run on the
  same git revision as it was compiled for. Replaced by a major/minor ABI counter.


Bugs fixed
----------

* 1269_ - Use correct byte counters in varnishncsa when piping a request.
* 1524_ - Chunked requests should be pipe-able.
* 1530_ - Expire old object on successful IMS fetch.
* 1475_ - time-to-first-byte in varnishncsa was potentially dishonest.
* 1480_ - Porting guide for 4.0 is incomplete.
* 1482_ - Inherit group memberships of -u specified user.
* 1473_ - Fail correctly in configure when rst2man is not found.
* 1486_ - Truncate negative Age values to zero.
* 1488_ - Don't panic on high request rates.
* 1489_ - req.esi should only be available in client threads.
* 1490_ - Fix thread leak when reducing number of threads.
* 1491_ - Reorder backend connection close procedure to help test cases.
* 1498_ - Prefix translated VCL names to avoid name clashes.
* 1499_ - Don't leak an objcore when HSH_Lookup returns expired object.
* 1493_ - vcl_purge can return synth or restart.
* 1476_ - Cope with systems having sys/endian.h and endian.h.
* 1496_ - varnishadm should be consistent in argv ordering.
* 1494_ - Don't panic on VCL-initiated retry after a backend 500 error.
* 1139_ - Also reset keep (for IMS) time when purging.
* 1478_ - Avoid panic when delivering an object that expires during delivery.
* 1504_ - ACLs can be unreferenced with vcc_err_unref=off set.
* 1501_ - Handle that a director couldn't pick a backend.
* 1495_ - Reduce WRK_SumStat contention.
* 1510_ - Complain on symbol reuse in VCL.
* 1514_ - Document storage.NAME.free_space and .used_space [docs]
* 1518_ - Suppress body on 304 response when using ESI.
* 1519_ - Round-robin director does not support weight. [docs]


.. _1269: https://www.varnish-cache.org/trac/ticket/1269
.. _1524: https://www.varnish-cache.org/trac/ticket/1524
.. _1530: https://www.varnish-cache.org/trac/ticket/1530
.. _1475: https://www.varnish-cache.org/trac/ticket/1475
.. _1480: https://www.varnish-cache.org/trac/ticket/1480
.. _1482: https://www.varnish-cache.org/trac/ticket/1482
.. _1473: https://www.varnish-cache.org/trac/ticket/1473
.. _1486: https://www.varnish-cache.org/trac/ticket/1486
.. _1488: https://www.varnish-cache.org/trac/ticket/1488
.. _1489: https://www.varnish-cache.org/trac/ticket/1489
.. _1490: https://www.varnish-cache.org/trac/ticket/1490
.. _1491: https://www.varnish-cache.org/trac/ticket/1491
.. _1498: https://www.varnish-cache.org/trac/ticket/1498
.. _1499: https://www.varnish-cache.org/trac/ticket/1499
.. _1493: https://www.varnish-cache.org/trac/ticket/1493
.. _1476: https://www.varnish-cache.org/trac/ticket/1476
.. _1496: https://www.varnish-cache.org/trac/ticket/1496
.. _1494: https://www.varnish-cache.org/trac/ticket/1494
.. _1139: https://www.varnish-cache.org/trac/ticket/1139
.. _1478: https://www.varnish-cache.org/trac/ticket/1478
.. _1504: https://www.varnish-cache.org/trac/ticket/1504
.. _1501: https://www.varnish-cache.org/trac/ticket/1501
.. _1495: https://www.varnish-cache.org/trac/ticket/1495
.. _1510: https://www.varnish-cache.org/trac/ticket/1510
.. _1518: https://www.varnish-cache.org/trac/ticket/1518
.. _1519: https://www.varnish-cache.org/trac/ticket/1519


==============================================
Changes from 4.0.0 beta1 to 4.0.0 (2014-04-10)
==============================================

New since 4.0.0-beta1:

- improved varnishstat documentation.
- In VCL, req.backend_hint is available in vcl_hit
- ncurses is now a dependency.


Bugs fixed
----------

* 1469_ - Fix build error on PPC
* 1468_ - Set ttl=0 on failed objects
* 1462_ - Handle duplicate ReqURL in varnishncsa.
* 1467_ - Fix missing clearing of oc->busyobj on HSH_Fail.


.. _1469: https://www.varnish-cache.org/trac/ticket/1469
.. _1468: https://www.varnish-cache.org/trac/ticket/1468
.. _1462: https://www.varnish-cache.org/trac/ticket/1462
.. _1467: https://www.varnish-cache.org/trac/ticket/1467


==================================================
Changes from 4.0.0 TP2 to 4.0.0 beta1 (2014-03-27)
==================================================

New since TP2:

- Previous always-appended code called default.vcl is now called builtin.vcl.
  The new example.vcl is recommended as a starting point for new users.
- vcl_error is now called vcl_synth, and does not any more mandate closing the
  client connection.
- New VCL function vcl_backend_error, where you can change the 503 prepared if
  all your backends are failing. This can then be cached as a regular object.
- Keyword "remove" in VCL is replaced by "unset".
- new timestamp and accounting records in varnishlog.
- std.timestamp() is introduced.
- stored objects are now read only, meaning obj.hits now counts per objecthead
  instead. obj.lastuse saw little use and has been removed.
- builtin VCL now does return(pipe) for chunked POST and PUT requests.
- python-docutils and rst2man are now build requirements.
- cli_timeout is now 60 seconds to avoid slaughtering the child process in
  times of high IO load/scheduling latency.
- return(purge) from vcl_recv is now valid.
- return(hash) is now the default return action from vcl_recv.
- req.backend is now req.backend_hint. beresp.storage is beresp.storage_hint.


Bugs fixed
----------

* 1460_ - tools now use the new timestamp format.
* 1450_ - varnishstat -l segmentation fault.
* 1320_ - Work around Content-Length: 0 and Content-Encoding: gzip gracefully.
* 1458_ - Panic on busy object.
* 1417_ - Handle return(abandon) in vcl_backend_response.
* 1455_ - vcl_pipe now sets Connection: close by default on backend requests.
* 1454_ - X-Forwarded-For is now done in C, before vcl_recv is run.
* 1436_ - Better explanation when missing an import in VCL.
* 1440_ - Serve ESI-includes from a different backend.
* 1441_ - Incorrect grouping when logging ESI subrequests.
* 1434_ - std.duration can now do ms/milliseconds.
* 1419_ - Don't put objcores on the ban list until they go non-BUSY.
* 1405_ - Ban lurker does not always evict all objects.

.. _1460: https://www.varnish-cache.org/trac/ticket/1460
.. _1450: https://www.varnish-cache.org/trac/ticket/1450
.. _1320: https://www.varnish-cache.org/trac/ticket/1320
.. _1458: https://www.varnish-cache.org/trac/ticket/1458
.. _1417: https://www.varnish-cache.org/trac/ticket/1417
.. _1455: https://www.varnish-cache.org/trac/ticket/1455
.. _1454: https://www.varnish-cache.org/trac/ticket/1454
.. _1436: https://www.varnish-cache.org/trac/ticket/1436
.. _1440: https://www.varnish-cache.org/trac/ticket/1440
.. _1441: https://www.varnish-cache.org/trac/ticket/1441
.. _1434: https://www.varnish-cache.org/trac/ticket/1434
.. _1419: https://www.varnish-cache.org/trac/ticket/1419
.. _1405: https://www.varnish-cache.org/trac/ticket/1405


================================================
Changes from 4.0.0 TP1 to 4.0.0 TP2 (2014-01-23)
================================================

New since from 4.0.0 TP1
------------------------

- New VCL_BLOB type to pass binary data between VMODs.
- New format for VMOD description files. (.vcc)

Bugs fixed
----------
* 1404_ - Don't send Content-Length on 304 Not Modified responses.
* 1401_ - Varnish would crash when retrying a backend fetch too many times.
* 1399_ - Memory get freed while in use by another thread/object
* 1398_ - Fix NULL deref related to a backend we don't know anymore.
* 1397_ - Crash on backend fetch while LRUing.
* 1395_ - End up in vcl_error also if fetch fails vcl_backend_response.
* 1391_ - Client abort and retry during a streaming fetch would make Varnish assert.
* 1390_ - Fix assert if the ban lurker is overtaken by new duplicate bans.
* 1385_ - ban lurker doesn't remove (G)one bans
* 1383_ - varnishncsa logs requests for localhost regardless of host header.
* 1382_ - varnishncsa prints nulls as part of request string.
* 1381_ - Ensure vmod_director is installed
* 1323_ - Add a missing boundary check for Range requests
* 1268_ - shortlived parameter now uses TTL+grace+keep instead of just TTL.

* Fix build error on OpenBSD (TCP_KEEP)
* n_object wasn't being decremented correctly on object expire.
* Example default.vcl in distribution is now 4.0-ready.

Open issues
-----------

* 1405_ - Ban lurker does not always evict all objects.


.. _1405: https://www.varnish-cache.org/trac/ticket/1405
.. _1404: https://www.varnish-cache.org/trac/ticket/1404
.. _1401: https://www.varnish-cache.org/trac/ticket/1401
.. _1399: https://www.varnish-cache.org/trac/ticket/1399
.. _1398: https://www.varnish-cache.org/trac/ticket/1398
.. _1397: https://www.varnish-cache.org/trac/ticket/1397
.. _1395: https://www.varnish-cache.org/trac/ticket/1395
.. _1391: https://www.varnish-cache.org/trac/ticket/1391
.. _1390: https://www.varnish-cache.org/trac/ticket/1390
.. _1385: https://www.varnish-cache.org/trac/ticket/1385
.. _1383: https://www.varnish-cache.org/trac/ticket/1383
.. _1382: https://www.varnish-cache.org/trac/ticket/1382
.. _1381: https://www.varnish-cache.org/trac/ticket/1381
.. _1323: https://www.varnish-cache.org/trac/ticket/1323
.. _1268: https://www.varnish-cache.org/trac/ticket/1268


============================================
Changes from 3.0.7-rc1 to 3.0.7 (2015-03-23)
============================================

- No changes.

============================================
Changes from 3.0.6 to 3.0.7-rc1 (2015-03-18)
============================================

- Requests with multiple Content-Length headers will now fail.

- Stop recognizing a single CR (\r) as a HTTP line separator.
  This opened up a possible cache poisoning attack in stacked installations
  where sslterminator/varnish/backend had different CR handling.

- Improved error detection on master-child process communication, leading to
  faster recovery (child restart) if communication loses sync.

- Fix a corner-case where Content-Length was wrong for HTTP 1.0 clients,
  when using gzip and streaming. Bug 1627_.

- More robust handling of hop-by-hop headers.

- [packaging] Coherent Redhat pidfile in init script. Bug 1690_.

- Avoid memory leak when adding bans.

.. _1627: http://varnish-cache.org/trac/ticket/1627
.. _1690: http://varnish-cache.org/trac/ticket/1690


===========================================
Changes from 3.0.6rc1 to 3.0.6 (2014-10-16)
===========================================

- Minor changes to documentation.
- [varnishadm] Add termcap workaround for libedit. Bug 1531_.


===========================================
Changes from 3.0.5 to 3.0.6rc1 (2014-06-24)
===========================================

- Document storage.<name>.* VCL variables. Bug 1514_.
- Fix memory alignment panic when http_max_hdr is not a multiple of 4. Bug 1327_.
- Avoid negative ReqEnd timestamps with ESI. Bug 1297_.
- %D format for varnishncsa is now an integer (as documented)
- Fix compile errors with clang.
- Clear objectcore flags earlier in ban lurker to avoid spinning thread. Bug 1470_.
- Patch embedded jemalloc to avoid segfault. Bug 1448_.
- Allow backend names to start with if, include or else. Bug 1439_.
- Stop handling gzip after gzip body end. Bug 1086_.
- Document %D and %T for varnishncsa.

.. _1514: https://www.varnish-cache.org/trac/ticket/1514
.. _1327: https://www.varnish-cache.org/trac/ticket/1327
.. _1297: https://www.varnish-cache.org/trac/ticket/1297
.. _1470: https://www.varnish-cache.org/trac/ticket/1470
.. _1448: https://www.varnish-cache.org/trac/ticket/1448
.. _1439: https://www.varnish-cache.org/trac/ticket/1439
.. _1086: https://www.varnish-cache.org/trac/ticket/1086


=============================================
Changes from 3.0.5 rc 1 to 3.0.5 (2013-12-02)
=============================================

varnishd
--------

- Always check the local address of a socket.  This avoids a crash if
  server.ip is accessed after a client has closed the connection. `Bug #1376`

.. _bug #1376: https://www.varnish-cache.org/trac/ticket/1376


================================
Changes from 3.0.4 to 3.0.5 rc 1
================================

varnishd
--------

- Stop printing error messages on ESI parse errors
- Fix a problem where Varnish would segfault if the first part of a
  synthetic page was NULL.  `Bug #1287`
- If streaming was used, you could in some cases end up with duplicate
  content headers being sent to clients. `Bug #1272`
- If we receive a completely garbled request, don't pass through
  vcl_error, since we could then end up in vcl_recv through a restart
  and things would go downhill from there. `Bug #1367`
- Prettify backtraces on panic slightly.

.. _bug #1287: https://www.varnish-cache.org/trac/ticket/1287
.. _bug #1272: https://www.varnish-cache.org/trac/ticket/1272
.. _bug #1367: https://www.varnish-cache.org/trac/ticket/1367

varnishlog
----------

- Correct an error where -m, -c and -b would interact badly, leading
  to lack of matches.  Also, emit BackendXID to signify the start of a
  transaction. `Bug #1325`

.. _bug #1325: https://www.varnish-cache.org/trac/ticket/1325

varnishadm
----------

- Handle input from stdin properly. `Bug #1314`

.. _bug #1314: https://www.varnish-cache.org/trac/ticket/1314


=============================================
Changes from 3.0.4 rc 1 to 3.0.4 (2013-06-14)
=============================================

varnishd
--------

- Set the waiter pipe as non-blocking and record overflows.  `Bug
  #1285`
- Fix up a bug in the ACL compile code that could lead to false
  negatives.  CVE-2013-4090.    `Bug #1312`
- Return an error if the client sends multiple Host headers.

.. _bug #1285: https://www.varnish-cache.org/trac/ticket/1285
.. _bug #1312: https://www.varnish-cache.org/trac/ticket/1312


================================
Changes from 3.0.3 to 3.0.4 rc 1
================================

varnishd
--------

- Fix error handling when uncompressing fetched objects for ESI
  processing. `Bug #1184`
- Be clearer about which timeout was reached in logs.
- Correctly decrement n_waitinglist counter.  `Bug #1261`
- Turn off Nagle/set TCP_NODELAY.
- Avoid panic on malformed Vary headers.  `Bug #1275`
- Increase the maximum length of backend names.  `Bug #1224`
- Add support for banning on http.status.  `Bug #1076`
- Make hit-for-pass correctly prefer the transient storage.

.. _bug #1076: https://www.varnish-cache.org/trac/ticket/1076
.. _bug #1184: https://www.varnish-cache.org/trac/ticket/1184
.. _bug #1224: https://www.varnish-cache.org/trac/ticket/1224
.. _bug #1261: https://www.varnish-cache.org/trac/ticket/1261
.. _bug #1275: https://www.varnish-cache.org/trac/ticket/1275


varnishlog
----------

- If -m, but neither -b or -c is given, assume both.  This filters out
  a lot of noise when -m is used to filter.  `Bug #1071`

.. _bug #1071: https://www.varnish-cache.org/trac/ticket/1071

varnishadm
----------

- Improve tab completion and require libedit/readline to build.

varnishtop
----------

- Reopen log file if Varnish is restarted.

varnishncsa
-----------

- Handle file descriptors above 64k (by ignoring them).  Prevents a
  crash in some cases with corrupted shared memory logs.
- Add %D and %T support for more timing information.

Other
-----

- Documentation updates.
- Fixes for OSX
- Disable PCRE JIT-er, since it's broken in some PCRE versions, at
  least on i386.
- Make libvarnish prefer exact hits when looking for VSL tags.


========================================
Changes from 3.0.2 to 3.0.3 (2012-08-20)
========================================

varnishd
--------

- Fix a race on the n_sess counter. This race made varnish do excessive
  session workspace allocations. `Bug #897`_.
- Fix some crashes in the gzip code when it runs out of memory. `Bug #1037`_.
  `Bug #1043`_. `Bug #1044`_.
- Fix a bug where the regular expression parser could end up in an infinite
  loop. `Bug #1047`_.
- Fix a memory leak in the regex code.
- DNS director now uses port 80 by default if not specified.
- Introduce `idle_send_timeout` and increase default value for `send_timeout`
  to 600s. This allows a long send timeout for slow clients while still being
  able to disconnect idle clients.
- Fix an issue where <esi:remove> did not remove HTML comments. `Bug #1092`_.
- Fix a crash when passing with streaming on.
- Fix a crash in the idle session timeout code.
- Fix an issue where the poll waiter did not timeout clients if all clients
  were idle. `Bug #1023`_.
- Log regex errors instead of crashing.
- Introduce `pcre_match_limit`, and `pcre_match_limit_recursion` parameters.
- Add CLI commands to manually control health state of a backend.
- Fix an issue where the s_bodybytes counter is not updated correctly on
  gunzipped delivery.
- Fix a crash when we couldn't allocate memory for a fetched object.
  `Bug #1100`_.
- Fix an issue where objects could end up in the transient store with a
  long TTL, when memory could not be allocated for them in the requested
  store. `Bug #1140`_.
- Activate req.hash_ignore_busy when req.hash_always_miss is activated.
  `Bug #1073`_.
- Reject invalid tcp port numbers for listen address. `Bug #1035`_.
- Enable JIT for better performing regular expressions. `Bug #1080`_.
- Return VCL errors in exit code when using -C. `Bug #1069`_.
- Stricter validation of acl syntax, to avoid a crash with 5-octet IPv4
  addresses. `Bug #1126`_.
- Fix a crash when first argument to regsub was null. `Bug #1125`_.
- Fix a case where varnish delivered corrupt gzip content when using ESI.
  `Bug #1109`_.
- Fix a case where varnish didn't remove the old Date header and served
  it alongside the varnish-generated Date header. `Bug #1104`_.
- Make saint mode work, for the case where we have no object with that hash.
  `Bug #1091`_.
- Don't save the object body on hit-for-pass objects.
- n_ban_gone counter added to count the number of "gone" bans.
- Ban lurker rewritten to properly sleep when no bans are present, and
  otherwise to process the list at the configured speed.
- Fix a case where varnish delivered wrong content for an uncompressed page
  with compressed ESI child. `Bug #1029`_.
- Fix an issue where varnish runs out of thread workspace when processing
  many ESI includes on an object. `Bug #1038`_.
- Fix a crash when streaming was enabled for an empty body.
- Better error reporting for some fetch errors.
- Small performance optimizations.

.. _bug #897: https://www.varnish-cache.org/trac/ticket/897
.. _bug #1023: https://www.varnish-cache.org/trac/ticket/1023
.. _bug #1029: https://www.varnish-cache.org/trac/ticket/1029
.. _bug #1035: https://www.varnish-cache.org/trac/ticket/1035
.. _bug #1037: https://www.varnish-cache.org/trac/ticket/1037
.. _bug #1038: https://www.varnish-cache.org/trac/ticket/1038
.. _bug #1043: https://www.varnish-cache.org/trac/ticket/1043
.. _bug #1044: https://www.varnish-cache.org/trac/ticket/1044
.. _bug #1047: https://www.varnish-cache.org/trac/ticket/1047
.. _bug #1069: https://www.varnish-cache.org/trac/ticket/1069
.. _bug #1073: https://www.varnish-cache.org/trac/ticket/1073
.. _bug #1080: https://www.varnish-cache.org/trac/ticket/1080
.. _bug #1091: https://www.varnish-cache.org/trac/ticket/1091
.. _bug #1092: https://www.varnish-cache.org/trac/ticket/1092
.. _bug #1100: https://www.varnish-cache.org/trac/ticket/1100
.. _bug #1104: https://www.varnish-cache.org/trac/ticket/1104
.. _bug #1109: https://www.varnish-cache.org/trac/ticket/1109
.. _bug #1125: https://www.varnish-cache.org/trac/ticket/1125
.. _bug #1126: https://www.varnish-cache.org/trac/ticket/1126
.. _bug #1140: https://www.varnish-cache.org/trac/ticket/1140

varnishncsa
-----------

- Support for \t\n in varnishncsa format strings.
- Add new format: %{VCL_Log:foo}x which output key:value from std.log() in
  VCL.
- Add user-defined date formatting, using %{format}t.

varnishtest
-----------

- resp.body is now available for inspection.
- Make it possible to test for the absence of an HTTP header. `Bug #1062`_.
- Log the full panic message instead of shortening it to 512 characters.

.. _bug #1062: https://www.varnish-cache.org/trac/ticket/1062

varnishstat
-----------

- Add json output (-j).

Other
-----

- Documentation updates.
- Bump minimum number of threads to 50 in RPM packages.
- RPM packaging updates.
- Fix some compilation warnings on Solaris.
- Fix some build issues on Open/Net/DragonFly-BSD.
- Fix build on FreeBSD 10-current.
- Fix libedit detection on \*BSD OSes. `Bug #1003`_.

.. _bug #1003: https://www.varnish-cache.org/trac/ticket/1003


=============================================
Changes from 3.0.2 rc 1 to 3.0.2 (2011-10-26)
=============================================

varnishd
--------

- Make the size of the synthetic object workspace equal to
  `http_resp_size` and add workaround to avoid a crash when setting
  too long response strings for synthetic objects.

- Ensure the ban lurker always sleeps the advertised 1 second when it
  does not have anything to do.

- Remove error from `vcl_deliver`.  Previously this would assert while
  it will now give a syntax error.

varnishncsa
-----------

- Add default values for some fields when logging incomplete records
  and document the default values.

Other
-----

- Documentation updates

- Some Solaris portability updates.


=============================================
Changes from 3.0.1 to 3.0.2 rc 1 (2011-10-06)
=============================================

varnishd
--------

- Only log the first 20 bytes of extra headers to prevent overflows.

- Fix crasher bug which sometimes happened if responses are queued and
  the backend sends us Vary. `Bug #994`_.

- Log correct size of compressed when uncompressing them for clients
  that do not support compression. `Bug #996`_.

- Only send Range responses if we are going to send a body. `Bug #1007`_.

- When varnishd creates a storage file, also unlink it to avoid
  leaking disk space over time.  `Bug #1008`_.

- The default size of the `-s file` parameter has been changed to
  100MB instead of 50% of the available disk space.

- The limit on the number of objects we remove from the cache to make
  room for a new one was mistakenly lowered to 10 in 3.0.1.  This has
  been raised back to 50.  `Bug #1012`_.

- `http_req_size` and `http_resp_size` have been increased to 8192
  bytes.  This better matches what other HTTPds have.   `Bug #1016`_.

.. _bug #994: https://www.varnish-cache.org/trac/ticket/994
.. _bug #992: https://www.varnish-cache.org/trac/ticket/992
.. _bug #996: https://www.varnish-cache.org/trac/ticket/996
.. _bug #1007: https://www.varnish-cache.org/trac/ticket/1007
.. _bug #1008: https://www.varnish-cache.org/trac/ticket/1008
.. _bug #1012: https://www.varnish-cache.org/trac/ticket/1012
.. _bug #1016: https://www.varnish-cache.org/trac/ticket/1016

VCL
---

- Allow relational comparisons of floating point types.

- Make it possible for VMODs to fail loading and so cause the VCL
  loading to fail.

varnishncsa
-----------

- Fixed crash when client was sending illegal HTTP headers.

- `%{Varnish:handling}` in format strings was broken, this has been
  fixed.

Other
-----

- Documentation updates

- Some Solaris portability updates.


=============================================
Changes from 3.0.1 rc 1 to 3.0.1 (2011-08-30)
=============================================

varnishd
--------

- Fix crash in streaming code.

- Add `fallback` director, as a variant of the `round-robin`
  director.

- The parameter `http_req_size` has been reduced on 32 bit machines.

VCL
---

- Disallow error in the `vcl_init` and `vcl_fini` VCL functions.

varnishncsa
-----------

- Fixed crash when using `-X`.

- Fix error when the time to first byte was in the format string.

Other
-----

- Documentation updates


=============================================
Changes from 3.0.0 to 3.0.1 rc 1 (2011-08-24)
=============================================

varnishd
--------

- Avoid sending an empty end-chunk when sending bodyless responses.

- `http_resp_hdr_len` and `http_req_hdr_len` were set to too low
  values leading to clients receiving `HTTP 400 Bad Request` errors.
  The limit has been increased and the error code is now `HTTP 413
  Request entity too large`.

- Objects with grace or keep set were mistakenly considered as
  candidates for the transient storage.  They now have their grace and
  keep limited to limit the memory usage of the transient stevedore.

- If a request was restarted from `vcl_miss` or `vcl_pass` it would
  crash.  This has been fixed.  `Bug #965`_.

- Only the first few clients waiting for an object from the backend
  would be woken up when object arrived and this lead to some clients
  getting stuck for a long time.  This has now been fixed. `Bug #963`_.

- The `hash` and `client` directors would mistakenly retry fetching an
  object from the same backend unless health probes were enabled.
  This has been fixed and it will now retry a different backend.

.. _bug #965: https://www.varnish-cache.org/trac/ticket/965
.. _bug #963: https://www.varnish-cache.org/trac/ticket/963

VCL
---

- Request specific variables such as `client.*` and `server.*` are now
  correctly marked as not available in `vcl_init` and `vcl_fini`.

- The VCL compiler would fault if two IP comparisons were done on the
  same line.  This now works correctly.  `Bug #948`_.

.. _bug #948: https://www.varnish-cache.org/trac/ticket/948

varnishncsa
-----------

- Add support for logging arbitrary request and response headers.

- Fix crashes if `hitmiss` and `handling` have not yet been set.

- Avoid printing partial log lines if there is an error in a format
  string.

- Report user specified format string errors better.

varnishlog
----------

- `varnishlog -r` now works correctly again and no longer opens the
  shared log file of the running Varnish.

Other
-----

- Various documentation updates.

- Minor compilation fixes for newer compilers.

- A bug in the ESI entity replacement parser has been fixed.  `Bug
  #961`_.

- The ABI of VMODs are now checked.  This will require a rebuild of
  all VMODs against the new version of Varnish.

.. _bug #961: https://www.varnish-cache.org/trac/ticket/961


=============================================
Changes from 3.0 beta 2 to 3.0.0 (2011-06-16)
=============================================

varnishd
--------

- Avoid sending an empty end-chunk when sending bodyless responses.

VCL
---

- The `synthetic` keyword has now been properly marked as only
  available in `vcl_deliver`.  `Bug #936`_.

.. _bug #936: https://www.varnish-cache.org/trac/ticket/936

varnishadm
----------

- Fix crash if the secret file was unreadable.  `Bug #935`_.

- Always exit if `varnishadm` can't connect to the backend for any
  reason.

.. _bug #935: https://www.varnish-cache.org/trac/ticket/935


=====================================
Changes from 3.0 beta 1 to 3.0 beta 2
=====================================

varnishd
--------

- thread_pool_min and thread_pool_max now each refer to the number of
  threads per pool, rather than being inconsistent as they were
  before.

- 307 Temporary redirect is now considered cacheable.  `Bug #908`_.

- The `stats` command has been removed from the CLI interface.  With
  the new counters, it would mean implementing more and more of
  varnishstat in the master CLI process and the CLI is
  single-threaded so we do not want to do this work there in the first
  place.  Use varnishstat instead.

.. _bug #908: https://www.varnish-cache.org/trac/ticket/908

VCL
---

- VCL now treats null arguments (unset headers for instance) as empty
  strings.  `Bug #913`_.

- VCL now has vcl_init and vcl_fini functions that are called when a
  given VCL has been loaded and unloaded.

- There is no longer any interpolation of the right hand side in bans
  where the ban is a single string.  This was confusing and you now
  have to make sure bits are inside or outside string context as
  appropriate.

- Varnish is now stricter in enforcing no duplication of probes,
  backends and ACLs.

.. _bug #913: https://www.varnish-cache.org/trac/ticket/913

varnishncsa
-----------

- varnishncsa now ignores piped requests, since we have no way of
  knowing their return status.

VMODs
-----

- The std module now has proper documentation, including a manual page


================================
Changes from 2.1.5 to 3.0 beta 1
================================

Upcoming changes
----------------

- The interpretation of bans will change slightly between 3.0 beta 1
  and 3.0 release.  Currently, doing ``ban("req.url == req.url")``
  will cause the right hand req.url to be interpreted in the context
  of the request creating the ban.  This will change so you will have
  to do ``ban("req.url == " + req.url)`` instead.  This syntax already
  works and is recommended.

varnishd
--------

- Add streaming on ``pass`` and ``miss``.  This is controlled by the
  ``beresp.do_stream`` boolean.  This includes support for
  compression/uncompression.
- Add support for ESI and gzip.
- Handle objects larger than 2G.
- HTTP Range support is now enabled by default
- The ban lurker is enabled by default
- if there is a backend or director with the name ``default``, use
  that as the default backend, otherwise use the first one listed.
- Add many more stats counters.  Amongst those, add per storage
  backend stats and per-backend statistics.
- Syslog the platform we are running on
- The ``-l`` (shared memory log file) argument has been changed,
  please see the varnishd manual for the new syntax.
- The ``-S`` and ``-T`` arguments are now stored in the shmlog
- Fix off-by-one error when exactly filling up the workspace.  `Bug #693`_.
- Make it possible to name storage backends.  The names have to be
  unique.
- Update usage output to match the code.  `Bug #683`_
- Add per-backend health information to shared memory log.
- Always recreate the shared memory log on startup.
- Add a ``vcl_dir`` parameter.  This is used to resolve relative path
  names for ``vcl.load`` and ``include`` in .vcl files.
- Make it possible to specify ``-T :0``.  This causes varnishd to look
  for a free port automatically.  The port is written in the shared
  memory log so varnishadm can find it.
- Classify locks into kinds and collect stats for each kind,
  recording the data in the shared memory log.
- Auto-detect necessary flags for pthread support and ``VCC_CC``
  flags.  This should make Varnish somewhat happier on Solaris.  `Bug
  #663`_
- The ``overflow_max`` parameter has been renamed to ``queue_max``.
- If setting a parameter fails, report which parameter failed as this
  is not obvious during startup.
- Add a parameter named ``shortlived``.  Objects whose TTL is less
  than the parameter go into transient (malloc) storage.
- Reduce the default ``thread_add_delay`` to 2ms.
- The ``max_esi_includes`` parameter has been renamed to
  ``max_esi_depth``.
- Hash string components are now logged by default.
- The default connect timeout parameter has been increased to 0.7
  seconds.
- The ``err_ttl`` parameter has been removed and is replaced by a
  setting in default.vcl.
- The default ``send_timeout`` parameter has been reduced to 1 minute.
- The default ``ban_lurker`` sleep has been set to 10ms.
- When an object is banned, make sure to set its grace to 0 as well.
- Add ``panic.show`` and ``panic.clear`` CLI commands.
- The default ``http_resp_hdr_len`` and ``http_req_hdr_len`` has been
  increased to 2048 bytes.
- If ``vcl_fetch`` results in ``restart`` or ``error``, close the
  backend connection rather than fetching the object.
- If allocating storage for an object, try reducing the chunk size
  before evicting objects to make room.  `Bug #880`_
- Add ``restart`` from ``vcl_deliver``.  `Bug #411`_
- Fix an off-by-up-to-one-minus-epsilon bug where if an object from
  the backend did not have a last-modified header we would send out a
  304 response which did include a ``Last-Modified`` header set to
  when we received the object.  However, we would compare the
  timestamp to the fractional second we got the object, meaning any
  request with the exact timestamp would get a ``200`` response rather
  than the correct ``304``.
- Fix a race condition in the ban lurker where a serving thread and
  the lurker would both look at an object at the same time, leading to
  Varnish crashing.
- If a backend sends a ``Content-Length`` header and we are streaming and
  we are not uncompressing it, send the ``Content-Length`` header on,
  allowing browsers to display a progress bar.
- All storage must be at least 1M large.  This is to prevent
  administrator errors when specifying the size of storage where the
  admin might have forgotten to specify units.

.. _bug #693: https://www.varnish-cache.org/trac/ticket/693
.. _bug #683: https://www.varnish-cache.org/trac/ticket/683
.. _bug #663: https://www.varnish-cache.org/trac/ticket/663
.. _bug #880: https://www.varnish-cache.org/trac/ticket/880
.. _bug #411: https://www.varnish-cache.org/trac/ticket/411

Tools
-----

common
******

- Add an ``-m $tag:$regex`` parameter, used for selecting some
  transactions.  The parameter can be repeated, in which case it is
  logically and-ed together.

varnishadm
**********

- varnishadm will now pick up the -S and -T arguments from the shared
  memory log, meaning just running it without any arguments will
  connect to the running varnish.  `Bug #875`_
- varnishadm now accepts an -n argument to specify the location of the
  shared memory log file
- add libedit support

.. _bug #875: https://www.varnish-cache.org/trac/ticket/875

varnishstat
***********

- reopen shared memory log if the varnishd process is restarted.
- Improve support for selecting some, but not all fields using the
  ``-f`` argument. Please see the documentation for further details on
  the use of ``-f``.
- display per-backend health information

varnishncsa
***********

- Report error if called with ``-i`` and ``-I`` as they do not make
  any sense for varnishncsa.
- Add custom log formats, specified with ``-F``.  Most of the Apache
  log formats are supported, as well as some Varnish-specific ones.
  See the documentation for further information.  `Bug #712`_ and `bug #485`_

.. _bug #712: https://www.varnish-cache.org/trac/ticket/712
.. _bug #485: https://www.varnish-cache.org/trac/ticket/485

varnishtest
***********

- add ``-l`` and ``-L`` switches which leave ``/tmp/vtc.*`` behind on
  error and unconditionally respectively.
- add ``-j`` parameter to run tests in parallell and use this by
  default.

varnishtop
**********

- add ``-p $period`` parameter.  The units in varnishtop were
  previously undefined, they are now in requests/period.  The default
  period is 60 seconds.

varnishlog
**********

- group requests by default.  This can be turned off by using ``-O``
- the ``-o`` parameter is now a no-op and is ignored.

VMODs
-----

- Add a std VMOD which includes a random function, log, syslog,
  fileread, collect,

VCL
---

- Change string concatenation to be done using ``+`` rather than
  implicitly.
- Stop using ``%xx`` escapes in VCL strings.
- Change ``req.hash += value`` to ``hash_data(value)``
- Variables in VCL now have distinct read/write access
- ``bereq.connect_timeout`` is now available in ``vcl_pipe``.
- Make it possible to declare probes outside of a director. Please see
  the documentation on how to do this.
- The VCL compiler has been reworked greatly, expanding its abilities
  with regards to what kinds of expressions it understands.
- Add ``beresp.backend.name``, ``beresp.backend.ip`` and
  ``beresp.backend.port`` variables.  They are only available from
  ``vcl_fetch`` and are read only.  `Bug #481`_
- The default VCL now calls pass for any objects where
  ``beresp.http.Vary == "*"``.  `Bug #787`_
- The ``log`` keyword has been moved to the ``std`` VMOD.
- It is now possible to choose which storage backend to be used
- Add variables ``storage.$name.free_space``,
  ``storage.$name.used_space`` and ``storage.$name.happy``
- The variable ``req.can_gzip`` tells us whether the client accepts
  gzipped objects or not.
- ``purge`` is now called ``ban``, since that is what it really is and
  has always been.
- ``req.esi_level`` is now available.  `Bug #782`_
- esi handling is now controlled by the ``beresp.do_esi`` boolean rather
  than the ``esi`` function.
- ``beresp.do_gzip`` and ``beresp.do_gunzip`` now control whether an
  uncompressed object should be compressed and a compressed object
  should be uncompressed in the cache.
- make it possible to control compression level using the
  ``gzip_level`` parameter.
- ``obj.cacheable`` and ``beresp.cacheable`` have been removed.
  Cacheability is now solely through the ``beresp.ttl`` and
  ``beresp.grace`` variables.
- setting the ``obj.ttl`` or ``beresp.ttl`` to zero now also sets the
  corresponding grace to zero.  If you want a non-zero grace, set
  grace after setting the TTL.
- ``return(pass)`` in ``vcl_fetch`` has been renamed to
  ``return(hit_for_pass)`` to make it clear that pass in ``vcl_fetch``
  and ``vcl_recv`` are different beasts.
- Add actual purge support.  Doing ``purge`` will remove an object and
  all its variants.

.. _bug #481: https://www.varnish-cache.org/trac/ticket/481
.. _bug #787: https://www.varnish-cache.org/trac/ticket/787
.. _bug #782: https://www.varnish-cache.org/trac/ticket/782


Libraries
---------

- ``libvarnishapi`` has been overhauled and the API has been broken.
  Please see git commit logs and the support tools to understand
  what's been changed.
- Add functions to walk over all the available counters.  This is
  needed because some of the counter names might only be available at
  runtime.
- Limit the amount of time varnishapi waits for a shared memory log
  to appear before returning an error.
- All libraries but ``libvarnishapi`` have been moved to a private
  directory as they are not for public consumption and have no ABI/API
  guarantees.

Other
-----

- Python is now required to build
- Varnish Cache is now consistently named Varnish Cache.
- The compilation process now looks for kqueue on NetBSD
- Make it possible to use a system jemalloc rather than the bundled
  version.
- The documentation has been improved all over and should now be in
  much better shape than before


========================================
Changes from 2.1.4 to 2.1.5 (2011-01-25)
========================================

varnishd
--------

-  On pass from vcl\_recv, we did not remove the backends Content-Length
   header before adding our own. This could cause confusion for browsers
   and has been fixed.

-  Make pass with content-length work again. An issue with regards to
   304, Content-Length and pass has been resolved.

-  An issue relating to passed requests with If-Modified-Since headers
   has been fixed. Varnish did not recognize that the 304-response did
   not have a body.

-  A potential lock-inversion with the ban lurker thread has been
   resolved.

-  Several build-dependency issues relating to rst2man have been fixed.
   Varnish should now build from source without rst2man if you are using
   tar-balls.

-  Ensure Varnish reads the expected last CRLF after chunked data from
   the backend. This allows re-use of the connection.

-  Remove a GNU Make-ism during make dist to make BSD happier.

-  Document the log, set, unset, return and restart statements in the
   VCL documentation.

-  Fix an embarrassingly old bug where Varnish would run out of
   workspace when requests come in fast over a single connection,
   typically during synthetic benchmarks.

-  Varnish will now allow If-Modified-Since requests to objects without
   a Last-Modified-header, and instead use the time the object was
   cached instead.

-  Do not filter out Content-Range headers in pass.

-  Require -d, -b, -f, -S or -T when starting varnishd. In human terms,
   this means that it is legal to start varnishd without a Vcl or
   backend, but only if you have a CLI channel of some kind.

-  Don't suppress Cache-Control headers in pass responses.

-  Merge multi-line Cache-Control and Vary header fields. Until now, no
   browsers have needed this, but Chromium seems to find it necessary to
   spread its Cache-Control across two lines, and we get to deal with
   it.

-  Make new-purge not touch busy objects. This fixes a potential crash
   when calling VRT\_purge.

-  If there are everal grace-able objects, pick the least expired one.

-  Fix an issue with varnishadm -T :6082 shorthand.

-  Add bourn-shell like "here" documents on the CLI. Typical usage:
   vcl.inline vcl\_new << 42 backend foo {...} sub vcl\_recv {...} 42

-  Add CLI version to the CLI-banner, starting with version 1.0 to mark
   here-documents.

-  Fix a problem with the expiry thread slacking off during high load.

varnishtest
-----------

-  Remove no longer existing -L option.


===========================
Changes from 2.1.3 to 2.1.4
===========================

varnishd
--------

-  An embarrassing typo in the new binary heap layout caused inflated
   obj/objcore/objhdr counts and could cause odd problems when the LRU
   expunge mechanism was invoked. This has been fixed.

-  We now have updated documentation in the reStructuredText format.
   Manual pages and reference documentation are both built from this.

-  We now include a DNS director which uses DNS for choosing which
   backend to route requests to. Please see the documentation for more
   details.

-  If you restarted a request, the HTTP header X-Forwarded-For would be
   updated multiple times. This has been fixed.

-  If a VCL contained a % sign, and the vcl.show CLI command was used,
   varnishd would crash. This has been fixed.

-  When doing a pass operation, we would remove the Content-Length, Age
   and Proxy-Auth headers. We are no longer doing this.

-  now has a string representation, making it easier to construct
   Expires headers in VCL.

-  In a high traffic environment, we would sometimes reuse a file
   descriptor before flushing the logs from a worker thread to the
   shared log buffer. This would cause confusion in some of the tools.
   This has been fixed by explicitly flushing the log when a backend
   connection is closed.

-  If the communication between the management and the child process
   gets out of sync, we have no way to recover. Previously, varnishd
   would be confused, but we now just kill the child and restart it.

-  If the backend closes the connection on us just as we sent a request
   to it, we retry the request. This should solve some interoperability
   problems with Apache and the mpm-itk multi processing module.

-  varnishd now only provides help output the current CLI session is
   authenticated for.

-  If the backend does not tell us which length indication it is using,
   we now assume the resource ends EOF at.

-  The client director now has a variable client.identity which is used
   to choose which backend should receive a given request.

-  The Solaris port waiter has been updated, and other portability fixes
   for Solaris.

-  There was a corner case in the close-down processing of pipes, this
   has now been fixed.

-  Previously, if we stopped polling a backend which was sick, it never
   got marked as healthy. This has now been changed.

-  It is now possible to specify ports as part of the .host field in
   VCL.

-  The synthetic counters were not locked properly, and so the sms\_
   counters could underflow. This has now been fixed.

-  The value of obj.status as a string in vcl\_error would not be
   correct in all cases. This has been fixed.

-  Varnish would try to trim storage segments completely filled when
   using the malloc stevedore and the object was received chunked
   encoding. This has been fixed.

-  If a buggy backend sends us a Vary header with two colons, we would
   previously abort. We now rather fix this up and ignore the extra
   colon.

-  req.hash\_always\_miss and req.hash\_ignore\_busy has been added, to
   make preloading or periodically refreshing content work better.

varnishncsa
-----------

-  varnishncsa would in some cases be confused by ESI requests and
   output invalid lines. This has now been fixed.

varnishlog
----------

-  varnishlog now allows -o and -u together.

varnishtop
----------

-  varnishtop would crash on 32 bit architectures. This has been fixed.

libvarnishapi
-------------

-  Regex inclusion and exclusion had problems with matching particular
   parts of the string being matched. This has been fixed.


===========================
Changes from 2.1.2 to 2.1.3
===========================

varnishd
--------

-  Improve scalability of critbit.

-  The critbit hash algorithm has now been tightened to make sure the
   tree is in a consistent state at all points, and the time we wait for
   an object to cool off after it is eligible for garbage collection has
   been tweaked.

-  Add log command to VCL. This emits a VCL\_log entry into the shared
   memory log.

-  Only emit Length and ReqEnd log entries if we actually have an XID.
   This should get rid of some empty log lines in varnishncsa.

-  Destroy directors in a predictable fashion, namely reverse of
   creation order.

-  Fix bug when ESI elements spanned storage elements causing a panic.

-  In some cases, the VCL compiler would panic instead of giving
   sensible messages. This has now been fixed.

-  Correct an off-by-one error when the requested range exceeds the size
   of an object.

-  Handle requests for the end of an object correctly.

-  Allow tabulator characters in the third field of the first line of
   HTTP requests

-  On Solaris, if the remote end sends us an RST, all system calls
   related to that socket will return EINVAL. We now handle this better.

libvarnishapi
-------------

-  The -X parameter didn't work correctly. This has been fixed.


===========================
Changes from 2.1.1 to 2.1.2
===========================

varnishd
--------

-  When adding Range support for 2.1.1, we accidentally introduced a
   bug which would append garbage to objects larger than the chunk size,
   by default 128k. Browsers would do the right thing due to
   Content-Length, but some load balancers would get very confused.


===========================
Changes from 2.1.1 to 2.1.1
===========================

varnishd
--------

-  The changelog in 2.1.0 included syntax errors, causing the generated
   changelog to be empty.

-  The help text for default\_grace was wrongly formatted and included a
   syntax error. This has now been fixed.

-  varnishd now closes the file descriptor used to read the management
   secret file (from the -S parameter).

-  The child would previously try to close every valid file descriptor,
   something which could cause problems if the file descriptor ulimit
   was set too high. We now keep track of all the file descriptors we
   open and only close up to that number.

-  ESI was partially broken in 2.1.0 due to a bug in the rollback of
   session workspace. This has been fixed.

-  Reject the authcommand rather than crash if there is no -S parameter
   given.

-  Align pointers in allocated objects. This will in theory make Varnish
   a tiny bit faster at the expense of slightly more memory usage.

-  Ensure the master process process id is updated in the shared memory
   log file after we go into the background.

-  HEAD requests would be converted to GET requests too early, which
   affected pass and pipe. This has been fixed.

-  Update the documentation to point out that the TTL is no longer taken
   into account to decide whether an object is cacheable or not.

-  Add support for completely obliterating an object and all variants of
   it. Currently, this has to be done using inline C.

-  Add experimental support for the Range header. This has to be enabled
   using the parameter http\_range\_support.

-  The critbit hasher could get into a deadlock and had a race
   condition. Both those have now been fixed.

varnishsizes
-----------~

-  varnishsizes, which is like varnishhist, but for the length of
   objects, has been added..


===========================
Changes from 2.0.6 to 2.1.0
===========================

varnishd
--------

-  Persistent storage is now experimentally supported using the
   persistent stevedore. It has the same command line arguments as the
   file stevedore.

-  obj.\* is now called beresp.\* in vcl\_fetch, and obj.\* is now
   read-only.

-  The regular expression engine is now PCRE instead of POSIX regular
   expressions.

-  req.\* is now available in vcl\_deliver.

-  Add saint mode where we can attempt to grace an object if we don't
   like the backend response for some reason.

   Related, add saintmode\_threshold which is the threshold for the
   number of objects to be added to the trouble list before the backend
   is considered sick.

-  Add a new hashing method called critbit. This autoscales and should
   work better on large object workloads than the classic hash. Critbit
   has been made the default hash algorithm.

-  When closing connections, we experimented with sending RST to free up
   load balancers and free up threads more quickly. This caused some
   problems with NAT routers and so has been reverted for now.

-  Add thread that checks objects against ban list in order to prevent
   ban list from growing forever. Note that this needs purges to be
   written so they don't depend on req.\*. Enabled by setting
   ban\_lurker\_sleep to a nonzero value.

-  The shared memory log file format was limited to maximum 64k
   simultaneous connections. This is now a 32 bit field which removes
   this limitation.

-  Remove obj\_workspace, this is now sized automatically.

-  Rename acceptors to waiters

-  vcl\_prefetch has been removed. It was never fully implemented.

-  Add support for authenticating CLI connections.

-  Add hash director that chooses which backend to use depending on
   req.hash.

-  Add client director that chooses which backend to use depending on
   the client's IP address. Note that this ignores the X-Forwarded-For
   header.

-  varnishd now displays a banner by default when you connect to the
   CLI.

-  Increase performance somewhat by moving statistics gathering into a
   per-worker structure that is regularly flushed to the global stats.

-  Make sure we store the header and body of object together. This may
   in some cases improve performance and is needed for persistence.

-  Remove client-side address accounting. It was never used for anything
   and presented a performance problem.

-  Add a timestamp to bans, so you can know how old they are.

-  Quite a few people got confused over the warning about not being able
   to lock the shared memory log into RAM, so stop warning about that.

-  Change the default CLI timeout to 10 seconds.

-  We previously forced all inserts into the cache to be GET requests.
   This has been changed to allow POST as well in order to be able to
   implement purge-on-POST semantics.

-  The CLI command stats now only lists non-zero values.

-  The CLI command stats now only lists non-zero values.

-  Use daemon(3) from libcompat on Darwin.

-  Remove vcl\_discard as it causes too much complexity and never
   actually worked particularly well.

-  Remove vcl\_timeout as it causes too much complexity and never
   actually worked particularly well.

-  Update the documentation so it refers to sess\_workspace, not
   http\_workspace.

-  Document the -i switch to varnishd as well as the server.identity and
   server.hostname VCL variables.

-  purge.hash is now deprecated and no longer shown in help listings.

-  When processing ESI, replace the five mandatory XML entities when we
   encounter them.

-  Add string representations of time and relative time.

-  Add locking for n\_vbe\_conn to make it stop underflowing.

-  When ESI-processing content, check for illegal XML character
   entities.

-  Varnish can now connect its CLI to a remote instance when starting
   up, rather than just being connected to.

-  It is no longer needed to specify the maximum number of HTTP headers
   to allow from backends. This is now a run-time parameter.

-  The X-Forwarded-For header is now generated by vcl\_recv rather than
   the C code.

-  It is now possible to not send all CLI traffic to syslog.

-  It is now possible to not send all CLI traffic to syslog.

-  In the case of varnish crashing, it now outputs a identifying string
   with the OS, OS revision, architecture and storage parameters
   together with the backtrace.

-  Use exponential backoff when we run out of file descriptors or
   sessions.

-  Allow setting backend timeouts to zero.

-  Count uptime in the shared memory log.

-  Try to detect the case of two running varnishes with the same shmlog
   and storage by writing the master and child process ids to the shmlog
   and refusing to start if they are still running.

-  Make sure to use EOF mode when serving ESI content to HTTP/1.0
   clients.

-  Make sure we close the connection if it either sends Connection:
   close or it is a HTTP/1.0 backend that does not send Connection:
   keep-alive.

-  Increase the default session workspace to 64k on 64-bit systems.

-  Make the epoll waiter use level triggering, not edge triggering as
   edge triggering caused problems on very busy servers.

-  Handle unforeseen client disconnections better on Solaris.

-  Make session lingering apply to new sessions, not just reused
   sessions.

varnishstat
-----------

-  Make use of the new uptime field in the shared memory log rather than
   synthesizing it from the start time.

varnishlog
----------

-  Exit at the end of the file when started with -d.

varnishadm
----------

-  varnishadm can now have a timeout when trying to connect to the
   running varnishd.

-  varnishadm now knows how to respond to the secret from a secured
   varnishd


===========================
Changes from 2.0.5 to 2.0.6
===========================

varnishd
--------

-  2.0.5 had an off-by-one error in the ESI handling causing includes to
   fail a large part of the time. This has now been fixed.

-  Try harder to not confuse backends when sending them backend probes.
   We half-closed the connection, something some backends thought meant
   we had dropped the connection. Stop doing so, and add the capability
   for specifying the expected response code.

-  In 2.0.5, session lingering was turned on. This caused statistics to
   not be counted often enough in some cases. This has now been fixed.

-  Avoid triggering an assert if the other end closes the connection
   while we are lingering and waiting for another request from them.

-  When generating backtraces, prefer the built-in backtrace function if
   such exists. This fixes a problem compiling 2.0.5 on Solaris.

-  Make it possible to specify the per-thread stack size. This might be
   useful on 32 bit systems with their limited address space.

-  Document the -C option to varnishd.


===========================
Changes from 2.0.4 to 2.0.5
===========================

varnishd
--------

-  Handle object workspace overruns better.

-  Allow turning off ESI processing per request by using set req.esi =
   off.

-  Tell the kernel that we expect to use the mmap-ed file in a random
   fashion. On Linux, this turns off/down readahead and increases
   performance.

-  Make it possible to change the maximum number of HTTP headers we
   allow by passing --with-max-header-fields=NUM rather than changing
   the code.

-  Implement support for HTTP continuation lines.

-  Change how connections are closed and only use SO\_LINGER for orderly
   connection closure. This should hopefully make worker threads less
   prone to hangups on network problems.

-  Handle multi-element purges correctly. Previously we ended up with
   parse errors when this was done from VCL.

-  Handle illegal responses from the backend better by serving a 503
   page rather than panic-ing.

-  When we run into an assertion that is not true, Varnish would
   previously dump a little bit of information about itself. Extend that
   information with a backtrace. Note that this relies on the varnish
   binary being unstripped.

-  Add a session\_max parameter that limits the maximum number of
   sessions we keep open before we start dropping new connections
   summarily.

-  Try to consume less memory when doing ESI processing by properly
   rolling back used workspace after processing an object. This should
   make it possible to turn sess\_workspace quite a bit for users with
   ESI-heavy pages.

-  Turn on session\_linger by default. Tests have shown that
   session\_linger helps a fair bit with performance.

-  Rewrite the epoll acceptor for better performance. This should lead
   to both higher processing rates and maximum number of connections on
   Linux.

-  Add If-None-Match support, this gives significant bandwidth savings
   for users with compliant browsers.

-  RFC2616 specifies that ETag, Content-Location, Expires, Cache-Control
   and Vary should be emitted when delivering a response with the 304
   response code.

-  Various fixes which makes Varnish compile and work on AIX.

-  Turn on TCP\_DEFER\_ACCEPT on Linux. This should make us less
   suspecible to denial of service attacks as well as give us slightly
   better performance.

-  Add an .initial property to the backend probe specification. This is
   the number of good probes we pretend to have seen. The default is one
   less than .threshold, which means the first probe will decide if we
   consider the backend healthy.

-  Make it possible to compare strings against other string-like
   objects, not just plain strings. This allows you to compare two
   headers, for instance.

-  When support for restart in vcl\_error was added, there was no check
   to prevent infinte recursion. This has now been fixed.

-  Turn on purge\_dups by default. This should make us consume less
   memory when there are many bans for the same pattern added.

-  Add a new log tag called FetchError which tries to explain why we
   could not fetch an object from the backend.

-  Change the default srcaddr\_ttl to 0. It is not used by anything and
   has been removed in the development version. This will increase
   performance somewhat.

varnishtop
----------

-  varnishtop did not handle variable-length log fields correctly. This
   is now fixed.

-  varnishtop previously did not print the name of the tag, which made
   it very hard to understand. We now print out the tag name.


===========================
Changes from 2.0.3 to 2.0.4
===========================

varnishd
--------

-  Make Varnish more portable by pulling in fixes for Solaris and
   NetBSD.

-  Correct description of -a in the manual page.

-  Ensure we are compiling in C99 mode.

-  If error was called with a null reason, we would crash on Solaris.
   Make sure this no longer happens.

-  Varnish used to crash if you asked it to use a non-existent waiter.
   This has now been fixed.

-  Add documentation to the default VCL explaining that using
   Connection: close in vcl\_pipe is generally a good idea.

-  Add minimal facility for dealing with TELNET option negotiation by
   returning WONT to DO and DONT requests.

-  If the backend is unhealthy, use a graced object if one is available.

-  Make server.hostname and server.identity available to VCL. The latter
   can be set with the -i parameter to varnishd.

-  Make restart available from vcl\_error.

-  Previously, only the TTL of an object was considered in whether it
   would be marked as cacheable. This has been changed to take the grace
   into consideration as well.

-  Previously, if an included ESI fragment had a zero size, we would
   send out a zero-sized chunk which signifies end-of-transmission. We
   now ignore zero-sized chunks.

-  We accidentally slept for far too long when we reached the maximum
   number of open file descriptors. This has been corrected and
   accept\_fd\_holdoff now works correctly.

-  Previously, when ESI processing, we did not look at the full length,
   but stopped at the first NULL byte. We no longer do that, enabling
   ESI processing of binary data.

varnishtest
-----------

-  Make sure system "..." returns successfully to ensure test failures
   do not go unnoticed.

-  Make it possible to send NULL bytes through the testing framework.


===========================
Changes from 2.0.2 to 2.0.3
===========================

varnishd
--------

-  Handle If-Modified-Since and ESI sub-objects better, fixing a problem
   where we sometimes neglected to insert included objects.

-  restart in vcl\_hit is now supported.

-  Setting the TTL of an object to 0 seconds would sometimes cause it to
   be delivered for up to one second - epsilon. This has been corrected
   and we should now never deliver those objects to other clients.

-  The malloc storage backend now prints the maximum storage size, just
   like the file backend.

-  Various small documentation bugs have been fixed.

-  Varnish did not set a default interval for backend probes, causing it
   to poll the backend continuously. This has been corrected.

-  Allow "true" and "false" when setting boolean parameters, in addition
   to on/off, enable/disable and yes/no.

-  Default to always talking HTTP 1.1 with the backend.

-  Varnish did not make sure the file it was loading was a regular file.
   This could cause Varnish to crash if it was asked to load a directory
   or other non-regular file. We now check that the file is a regular
   file before loading it.

-  The binary heap used for expiry processing had scalability problems.
   Work around this by using stripes of a fixed size, which should make
   this scale better, particularly when starting up and having lots of
   objects.

-  When we imported the jemalloc library into the Varnish tree, it did
   not compile without warnings. This has now been fixed.

-  Varnish took a very long time to detect that the backend did not
   respond. To remedy this, we now have read timeouts in addition to the
   connect timeout. Both the first\_byte\_timeout and the
   between\_bytes\_timeout defaults to 60 seconds. The connect timeout
   is no longer in milliseconds, but rather in seconds.

-  Previously, the VCL to C conversion as well as the invocation of the
   C compiler was done in the management process. This is now done in a
   separate sub-process. This prevents any bugs in the VCL compiler from
   affecting the management process.

-  Chunked encoding headers were counted in the statistics for header
   bytes. They no longer are.

-  ESI processed objects were not counted in the statistics for body
   bytes. They now are.

-  It is now possible to adjust the maximum record length of log entries
   in the shmlog by tuning the shm\_reclen parameter.

-  The management parameters listed in the CLI were not sorted, which
   made it hard to find the parameter you were looking for. They are now
   sorted, which should make this easier.

-  Add a new hashing type, "critbit", which uses a lock-less tree based
   lookup algorithm. This is experimental and should not be enabled in
   production environments without proper testing.

-  The session workspace had a default size of 8k. It is now 16k, which
   should make VCLs where many headers are processed less prone to
   panics.

-  We have seen that people seem to be confused as to which actions in
   the different VCL functions return and which ones don't. Add a new
   syntax return(action) to make this more explicit. The old syntax is
   still supported.

-  Varnish would return an error if any of the management IPs listed in
   the -T parameter could not be listened to. We now only return an
   error if none of them can be listened to.

-  In the case of the backend or client giving us too many parameters,
   we used to just ignore the overflowing headers. This is problematic
   if you end up ignoreing Content-Length, Transfer-Encoding and similar
   headers. We now give out a 400 error to the client if it sends us too
   many and 503 if we get too many from the backend.

-  We used panic if we got a too large chunked header. This behaviour
   has been changed into just failing the transaction.

-  Varnish now supports an extended purge method where it is possible to
   do purge req.http.host ~ "web1.com" && req.url ~ "\\.png" and
   similar. See the documentation for details.

-  Under heavy load, Varnish would sometimes crash when trying to update
   the per-request statistics. This has now been fixed.

-  It is now possible to not save the hash string in the session and
   object workspace. This will save a lot of memory on sites with many
   small objects. Disabling the purge\_hash parameter also disables the
   purge.hash facility.

-  Varnish now supports !~ as a "no match" regular expression matcher.

-  In some cases, you could get serialised access to "pass" objects. We
   now make it default to the default\_ttl value; this can be overridden
   in vcl\_fetch.

-  Varnish did not check the syntax of regsub calls properly. More
   checking has been added.

-  If the client closed the connection while Varnish was processing ESI
   elements, Varnish would crash while trying to write the object to the
   client. We now check if the client has closed the connection.

-  The ESI parser had a bug where it would crash if an XML comment would
   span storage segments. This has been fixed.

VCL Manual page
--------------~

-  The documentation on how capturing parentheses work was wrong. This
   has been corrected.

-  Grace has now been documented.

varnishreplay
-------------

-  varnishreplay did not work correctly on Linux, due to a too small
   stack. This has now been fixed.


===========================
Changes from 2.0.1 to 2.0.2
===========================

varnishd
--------

-  In high-load situations, when using ESI, varnishd would sometimes
   mishandle objects and crash. This has been worked around.

varnishreplay
-------------

-  varnishreplay did not work correctly on Linux, due to a too small
   stack. This has now been fixed.


=========================
Changes from 2.0 to 2.0.1
=========================

varnishd
--------

-  When receiving a garbled HTTP request, varnishd would sometimes
   crash. This has been fixed.

-  There was an off-by-one error in the ACL compilation. Now fixed.

Red Hat spec file
----------------~

-  A typo in the spec file made the .rpm file names wrong.


=========================
Changes from 1.1.2 to 2.0
=========================

varnishd
--------

-  Only look for sendfile on platforms where we know how to use it,
   which is FreeBSD for now.

-  Make it possible to adjust the shared memory log size and bump the
   size from 8MB to 80MB.

-  Fix up the handling of request bodies to better match what RFC2616
   mandates. This makes PUT, DELETE, OPTIONS and TRACE work in addition
   to POST.

-  Change how backends are defined, to a constant structural definition
   style. See https://www.varnish-cache.org/wiki/VclSyntaxChanges
   for the details.

-  Add directors, which wrap backends. Currently, there's a random
   director and a round-robin director.

-  Add "grace", which is for how long and object will be served, even
   after it has expired. To use this, both the object's and the
   request's grace parameter need to be set.

-  Manual pages have been updated for new VCL syntax and varnishd
   options.

-  Man pages and other docs have been updated.

-  The shared memory log file is now locked in memory, so it should not
   be paged out to disk.

-  We now handle Vary correctly, as well as Expect.

-  ESI include support is implemented.

-  Make it possible to limit how much memory the malloc uses.

-  Solaris is now supported.

-  There is now a regsuball function, which works like regsub except it
   replaces all occurrences of the regex, not just the first.

-  Backend and director declarations can have a .connect\_timeout
   parameter, which tells us how long to wait for a successful
   connection.

-  It is now possible to select the acceptor to use by changing the
   acceptor parameter.

-  Backends can have probes associated with them, which can be checked
   with req.backend.health in VCL as well as being handled by directors
   which do load-balancing.

-  Support larger-than-2GB files also on 32 bit hosts. Please note that
   this does not mean we can support caches bigger than 2GB, it just
   means logfiles and similar can be bigger.

-  In some cases, we would remove the wrong header when we were
   stripping Content-Transfer-Encoding headers from a request. This has
   been fixed.

-  Backends can have a .max\_connections associated with them.

-  On Linux, we need to set the dumpable bit on the child if we want
   core dumps. Make sure it's set.

-  Doing purge.hash() with an empty string would cause us to dump core.
   Fixed so we don't do that anymore.

-  We ran into a problem with glibc's malloc on Linux where it seemed
   like it failed to ever give memory back to the OS, causing the system
   to swap. We have now switched to jemalloc which appears not to have
   this problem.

-  max\_restarts was never checked, so we always ended up running out of
   workspace. Now, vcl\_error is called when we reach max\_restarts.

varnishtest
-----------

-  varnishtest is a tool to do correctness tests of varnishd. The test
   suite is run by using make check.

varnishtop
----------

-  We now set the field widths dynamically based on the size of the
   terminal and the name of the longest field.

varnishstat
-----------

-  varnishstat -1 now displays the uptime too.

varnishncsa
-----------

-  varnishncsa now does fflush after each write. This makes tail -f work
   correctly, as well as avoiding broken lines in the log file.

-  It is possible to get varnishncsa to output the X-Forwarded-For
   instead of the client IP by passing -f to it.

Build system
-----------~

-  Various sanity checks have been added to configure, it now complains
   about no ncurses or if SO\_RCVTIMEO or SO\_SNDTIMEO are
   non-functional. It also aborts if there's no working acceptor
   mechanism

-  The C compiler invocation is decided by the configure script and can
   now be overridden by passing VCC\_CC when running configure.


===========================
Changes from 1.1.1 to 1.1.2
===========================

varnishd
--------

-  When switching to a new VCL configuration, a race condition exists
   which may cause Varnish to reference a backend which no longer exists
   (see `ticket #144 <https://www.varnish-cache.org/trac/ticket/144>`_).
   This race condition has not been entirely eliminated, but it should
   occur less frequently.

-  When dropping a TCP session before any requests were processed, an
   assertion would be triggered due to an uninitialized timestamp (see
   `ticket #132 <https://www.varnish-cache.org/trac/ticket/132>`_). The
   timestamp is now correctly initialized.

-  Varnish will now correctly generate a Date: header for every response
   instead of copying the one it got from the backend (see `ticket
   #157 <https://www.varnish-cache.org/trac/ticket/157>`_).

-  Comparisons in VCL which involve a non-existent string (usually a
   header which is not present in the request or object being processed)
   would cause a NULL pointer dereference; now the comparison will
   simply fail.

-  A bug in the VCL compiler which would cause a double-free when
   processing include directives has been fixed.

-  A resource leak in the worker thread management code has been fixed.

-  When connecting to a backend, Varnish will usually get the address
   from a cache. When the cache is refreshed, existing connections may
   end up with a reference to an address structure which no longer
   exists, resulting in a crash. This race condition has been somewhat
   mitigated, but not entirely eliminated (see `ticket
   #144 <https://www.varnish-cache.org/trac/ticket/144>`_.)

-  Varnish will now pass the correct protocol version in pipe mode: the
   backend will get what the client sent, and vice versa.

-  The core of the pipe mode code has been rewritten to increase
   robustness and eliminate spurious error messages when either end
   closes the connection in a manner Varnish did not anticipate.

-  A memory leak in the backend code has been plugged.

-  When using the kqueue acceptor, if a client shuts down the request
   side of the connection (as many clients do after sending their final
   request), it was possible for the acceptor code to receive the EOF
   event and recycle the session while the last request was still being
   serviced, resulting in a assertion failure and a crash when the
   worker thread later tried to delete the session. This should no
   longer happen (see `ticket
   #162 <https://www.varnish-cache.org/trac/ticket/162>`_.)

-  A mismatch between the recorded length of a cached object and the
   amount of data actually present in cache for that object can
   occasionally occur (see `ticket
   #167 <https://www.varnish-cache.org/trac/ticket/167>`_.) This has been
   partially fixed, but may still occur for error pages generated by
   Varnish when a problem arises while retrieving an object from the
   backend.

-  Some socket-related system calls may return unexpected error codes
   when operating on a TCP connection that has been shut down at the
   other end. These error codes would previously cause assertion
   failures, but are now recognized as harmless conditions.

varnishhist
-----------

-  Pressing 0 though 9 while varnishhist is running will change the
   refresh interval to the corresponding power of two, in seconds.

varnishncsa
-----------

-  The varnishncsa tool can now daemonize and write a PID file like
   varnishlog, using the same command-line options. It will also reopen
   its output upon receipt of a SIGHUP if invoked with -w.

varnishstat
-----------

-  Pressing 0 though 9 while varnishstat is running will change the
   refresh interval to the corresponding power of two, in seconds.

Build system
-----------~

-  Varnish's <queue.h> has been modified to avoid conflicts with
   <sys/queue.h> on platforms where the latter is included indirectly
   through system headers.

-  Several steps have been taken towards Solaris support, but this is
   not yet complete.

-  When configure was run without an explicit prefix, Varnish's idea of
   the default state directory would be garbage and a state directory
   would have to be specified manually with -n. This has been corrected.


=========================
Changes from 1.1 to 1.1.1
=========================

varnishd
--------

-  The code required to allow VCL to read obj.status, which had
   accidentally been left out, has now been added.

-  Varnish will now always include a Connection: header in its reply to
   the client, to avoid possible misunderstandings.

-  A bug that triggered an assertion failure when generating synthetic
   error documents has been corrected.

-  A new VCL function, purge\_url, provides the same functionality as
   the url.purge management command.

-  Previously, Varnish assumed that the response body should be sent
   only if the request method was GET. This was a problem for custom
   request methods (such as PURGE), so the logic has been changed to
   always send the response body except in the specific case of a HEAD
   request.

-  Changes to run-time parameters are now correctly propagated to the
   child process.

-  Due to the way run-time parameters are initialized at startup,
   varnishd previously required the nobody user and the nogroup group to
   exist even if a different user and group were specified on the
   command line. This has been corrected.

-  Under certain conditions, the VCL compiler would carry on after a
   syntax error instead of exiting after reporting the error. This has
   been corrected.

-  The manner in which the hash string is assembled has been modified to
   reduce memory usage and memory-to-memory copying.

-  Before calling vcl\_miss, Varnish assembles a tentative request
   object for the backend request which will usually follow. This object
   would be leaked if vcl\_miss returned anything else than fetch. This
   has been corrected.

-  The code necessary to handle an error return from vcl\_fetch and
   vcl\_deliver had inadvertantly been left out. This has been
   corrected.

-  Varnish no longer prints a spurious "child died" message (the result
   of reaping the compiler process) after compiling a new VCL
   configuration.

-  Under some circumstances, due to an error in the workspace management
   code, Varnish would lose the "tail" of a request, i.e. the part of
   the request that has been received from the client but not yet
   processed. The most obvious symptom of this was that POST requests
   would work with some browsers but not others, depending on details of
   the browser's HTTP implementation. This has been corrected.

-  On some platforms, due to incorrect assumptions in the CLI code, the
   management process would crash while processing commands received
   over the management port. This has been corrected.

Build system
-----------~

-  The top-level Makefile will now honor $DESTDIR when creating the
   state directory.

-  The Debian and RedHat packages are now split into three (main / lib /
   devel) as is customary.

-  A number of compile-time and run-time portability issues have been
   addressed.

-  The autogen.sh script had workarounds for problems with the GNU
   autotools on FreeBSD; these are no longer needed and have been
   removed.

-  The libcompat library has been renamed to libvarnishcompat and is now
   dynamic rather than static. This simplifies the build process and
   resolves an issue with the Mac OS X linker.


=========================
Changes from 1.0.4 to 1.1
=========================

varnishd
--------

-  Readability of the C source code generated from VCL code has been
   improved.

-  Equality (==) and inequality (!=) operators have been implemented for
   IP addresses (which previously could only be compared using ACLs).

-  The address of the listening socket on which the client connection
   was received is now available to VCL as the server.ip variable.

-  Each object's hash key is now computed based on a string which is
   available to VCL as req.hash. A VCL hook named vcl\_hash has been
   added to allow VCL scripts to control hash generation (for instance,
   whether or not to include the value of the Host: header in the hash).

-  The setup code for listening sockets has been modified to detect and
   handle situations where a host name resolves to multiple IP
   addresses. It will now attempt to bind to each IP address separately,
   and report a failure only if none of them worked.

-  Network or protocol errors that occur while retrieving an object from
   a backend server now result in a synthetic error page being inserted
   into the cache with a 30-second TTL. This should help avoid driving
   an overburdened backend server into the ground by repeatedly
   requesting the same object.

-  The child process will now drop root privileges immediately upon
   startup. The user and group to use are specified with the user and
   group run-time parameters, which default to nobody and nogroup,
   respectively. Other changes have been made in an effort to increase
   the isolation between parent and child, and reduce the impact of a
   compromise of the child process.

-  Objects which are received from the backend with a Vary: header are
   now stored separately according to the values of the headers
   specified in Vary:. This allows Varnish to correctly cache e.g.
   compressed and uncompressed versions of the same object.

-  Each Varnish instance now has a name, which by default is the host
   name of the machine it runs on, but can be any string that would be
   valid as a relative or absolute directory name. It is used to
   construct the name of a directory in which the server state as well
   as all temporary files are stored. This makes it possible to run
   multiple Varnish instances on the same machine without conflict.

-  When invoked with the -C option, varnishd will now not just translate
   the VCL code to C, but also compile the C code and attempt to load
   the resulting shared object.

-  Attempts by VCL code to reference a variable outside its scope or to
   assign a value to a read-only variable will now result in
   compile-time rather than run-time errors.

-  The new command-line option -F will make varnishd run in the
   foreground, without enabling debugging.

-  New VCL variables have been introduced to allow inspection and
   manipulation of the request sent to the backend (bereq.request,
   bereq.url, bereq.proto and bereq.http) and the response to the client
   (resp.proto, resp.status, resp.response and resp.http).

-  Statistics from the storage code (including the amount of data and
   free space in the cache) are now available to varnishstat and other
   statistics-gathering tools.

-  Objects are now kept on an LRU list which is kept loosely up-to-date
   (to within a few seconds). When cache runs out, the objects at the
   tail end of the LRU list are discarded one by one until there is
   enough space for the freshly requested object(s). A VCL hook,
   vcl\_discard, is allowed to inspect each object and determine its
   fate by returning either keep or discard.

-  A new VCL hook, vcl\_deliver, provides a chance to adjust the
   response before it is sent to the client.

-  A new management command, vcl.show, displays the VCL source code of
   any loaded configuration.

-  A new VCL variable, now, provides VCL scripts with the current time
   in seconds since the epoch.

-  A new VCL variable, obj.lastuse, reflects the time in seconds since
   the object in question was last used.

-  VCL scripts can now add an HTTP header (or modify the value of an
   existing one) by assigning a value to the corresponding variable, and
   strip an HTTP header by using the remove keyword.

-  VCL scripts can now modify the HTTP status code of cached objects
   (obj.status) and responses (resp.status)

-  Numeric and other non-textual variables in VCL can now be assigned to
   textual variables; they will be converted as needed.

-  VCL scripts can now apply regular expression substitutions to textual
   variables using the regsub function.

-  A new management command, status, returns the state of the child.

-  Varnish will now build and run on Mac OS X.

varnishadm
----------

-  This is a new utility which sends a single command to a Varnish
   server's management port and prints the result to stdout, greatly
   simplifying the use of the management port from scripts.

varnishhist
-----------

-  The user interface has been greatly improved; the histogram will be
   automatically rescaled and redrawn when the window size changes, and
   it is updated regularly rather than at a rate dependent on the amount
   of log data gathered. In addition, the name of the Varnish instance
   being watched is displayed in the upper right corner.

varnishncsa
-----------

-  In addition to client traffic, varnishncsa can now also process log
   data from backend traffic.

-  A bug that would cause varnishncsa to segfault when it encountered an
   empty HTTP header in the log file has been fixed.

varnishreplay
-------------

-  This new utility will attempt to recreate the HTTP traffic which
   resulted in the raw Varnish log data which it is fed.

varnishstat
-----------

-  Don't print lifetime averages when it doesn't make any sense, for
   instance, there is no point in dividing the amount in bytes of free
   cache space by the lifetime in seconds of the varnishd process.

-  The user interface has been greatly improved; varnishstat will no
   longer print more than fits in the terminal, and will respond
   correctly to window resize events. The output produced in one-shot
   mode has been modified to include symbolic names for each entry. In
   addition, the name of the Varnish instance being watched is displayed
   in the upper right corner in curses mode.

varnishtop
----------

-  The user interface has been greatly improved; varnishtop will now
   respond correctly to window resize events, and one-shot mode (-1)
   actually works. In addition, the name of the Varnish instance being
   watched is displayed in the upper right corner in curses mode.


===========================
Changes from 1.0.3 to 1.0.4
===========================

varnishd
--------

-  The request workflow has been redesigned to simplify request
   processing and eliminate code duplication. All codepaths which need
   to speak HTTP now share a single implementation of the protocol. Some
   new VCL hooks have been added, though they aren't much use yet. The
   only real user-visible change should be that Varnish now handles
   persistent backend connections correctly (see `ticket
   #56 <https://www.varnish-cache.org/trac/ticket/56>`_).

-  Support for multiple listen addresses has been added.

-  An "include" facility has been added to VCL, allowing VCL code to
   pull in code fragments from multiple files.

-  Multiple definitions of the same VCL function are now concatenated
   into one in the order in which they appear in the source. This
   simplifies the mechanism for falling back to the built-in default for
   cases which aren't handled in custom code, and facilitates
   modularization.

-  The code used to format management command arguments before passing
   them on to the child process would underestimate the amount of space
   needed to hold each argument once quotes and special characters were
   properly escaped, resulting in a buffer overflow. This has been
   corrected.

-  The VCL compiler has been overhauled. Several memory leaks have been
   plugged, and error detection and reporting has been improved
   throughout. Parts of the compiler have been refactored to simplify
   future extension of the language.

-  A bug in the VCL compiler which resulted in incorrect parsing of the
   decrement (-=) operator has been fixed.

-  A new -C command-line option has been added which causes varnishd to
   compile the VCL code (either from a file specified with -f or the
   built-in default), print the resulting C code and exit.

-  When processing a backend response using chunked encoding, if a chunk
   header crosses a read buffer boundary, read additional bytes from the
   backend connection until the chunk header is complete.

-  A new ping\_interval run-time parameter controls how often the
   management process checks that the worker process is alive.

-  A bug which would cause the worker process to dereference a NULL
   pointer and crash if the backend did not respond has been fixed.

-  In some cases, such as when they are used by AJAX applications to
   circumvent Internet Explorer's over-eager disk cache, it may be
   desirable to cache POST requests. However, the code path responsible
   for delivering objects from cache would only transmit the response
   body when replying to a GET request. This has been extended to also
   apply to POST.

   This should be revisited at a later date to allow VCL code to control
   whether the body is delivered.

-  Varnish now respects Cache-control: s-maxage, and prefers it to
   Cache-control: max-age if both are present.

   This should be revisited at a later date to allow VCL code to control
   which headers are used and how they are interpreted.

-  When loading a new VCL script, the management process will now load
   the compiled object to verify that it links correctly before
   instructing the worker process to load it.

-  A new -P command-line options has been added which causes varnishd to
   create a PID file.

-  The sendfile\_threshold run-time parameter's default value has been
   set to infinity after a variety of sendfile()-related bugs were
   discovered on several platforms.

varnishlog
----------

-  When grouping log entries by request, varnishlog attempts to collapse
   the log entry for a call to a VCL function with the log entry for the
   corresponding return from VCL. When two VCL calls were made in
   succession, varnishlog would incorrectly omit the newline between the
   two calls (see `ticket
   #95 <https://www.varnish-cache.org/trac/ticket/95>`_).

-  New -D and -P command-line options have been added to daemonize and
   create a pidfile, respectively.

-  The flag that is raised upon reception of a SIGHUP has been marked
   volatile so it will not be optimized away by the compiler.

varnishncsa
-----------

-  The formatting callback has been largely rewritten for clarity,
   robustness and efficiency.

   If a request included a Host: header, construct and output an
   absolute URL. This makes varnishncsa output from servers which handle
   multiple virtual hosts far more useful.

-  The flag that is raised upon reception of a SIGHUP has been marked
   volatile so it will not be optimized away by the compiler.

Documentation
-------------

-  The documentation, especially the VCL documentation, has been greatly
   extended and improved.

Build system
------------

-  The name and location of the curses or ncurses library is now
   correctly detected by the configure script instead of being hardcoded
   into affected Makefiles. This allows Varnish to build correctly on a
   wider range of platforms.

-  Compatibility shims for clock\_gettime() are now correctly applied
   where needed, allowing Varnish to build on MacOS X.

-  The autogen.sh script will now correctly detect and warn about
   automake versions which are known not to work correctly.
