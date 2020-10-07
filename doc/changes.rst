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

================================
Varnish Cache 6.0.7 (YYYY-MM-DD)
================================

* Add support for more HTTP response code reasons (3428_).

* Add weighted backends to vmod_shard.

* Fix an issue where an undefined value is returned when converting
  a VCL_TIME to a string. (3308_)

* Fix an issue where the wrong workspace is used during `vcl_pipe`.
  (3329_) (3361_) (3385_)

* Fix an assert situation when running out of workspace during H2
  delivery. (3382_)

* Fix an issue where we can incorrectly re-use a req.body. This
  introduces a new bereq body attribute: `bo->bereq_body`. (3093_)

* Fix `send_timeout` for certain kinds of slow HTTP1 writes. (3189_)

* Fix std.rollback() when not followed by a restart/retry. (3009_)

* Fix an assert situation when running out of workspace during ESI
  processing. (3253_)

* Don't override Content-Encoding on 304 responses. (3169_)

* Better thread creation signalling. (2942_)

* Fix an error where when using a certain internal status code will
  trigger an assert. (3301_)

* Fix a situation where a closed connection is recycled. (3266_)

* Fix an assert situation when doing a conditional fetch. (3273_)

* Fix an issue where `varnishadm` improperly returns an empty 200 response
  when you overflow. (3038_)

* Add new VCL parameters to warn when too many VCLs are loaded:
  `max_vcl` and `max_vcl_handling`. (2713_)

* Fix an issue where we can use a streaming object in a conditional
  fetch too soon. (3089_)

* Fix an issue where the Age is dropped when passing. (3221_)

* Expose the master and worker PIDs via the CLI: `varnishadm pid`. (3171_)

.. _3428: https://github.com/varnishcache/varnish-cache/pull/3428
.. _3308: https://github.com/varnishcache/varnish-cache/pull/3308
.. _3385: https://github.com/varnishcache/varnish-cache/issues/3385
.. _3361: https://github.com/varnishcache/varnish-cache/issues/3361
.. _3329: https://github.com/varnishcache/varnish-cache/issues/3329
.. _3382: https://github.com/varnishcache/varnish-cache/issues/3382
.. _3093: https://github.com/varnishcache/varnish-cache/pull/3093
.. _3189: https://github.com/varnishcache/varnish-cache/issues/3189
.. _3009: https://github.com/varnishcache/varnish-cache/issues/3009
.. _3253: https://github.com/varnishcache/varnish-cache/issues/3253
.. _3169: https://github.com/varnishcache/varnish-cache/issues/3169
.. _2942: https://github.com/varnishcache/varnish-cache/pull/2942
.. _3301: https://github.com/varnishcache/varnish-cache/issues/3301
.. _3266: https://github.com/varnishcache/varnish-cache/issues/3266
.. _3273: https://github.com/varnishcache/varnish-cache/issues/3273
.. _3038: https://github.com/varnishcache/varnish-cache/issues/3038
.. _2713: https://github.com/varnishcache/varnish-cache/issues/2713
.. _3089: https://github.com/varnishcache/varnish-cache/issues/3089
.. _3221: https://github.com/varnishcache/varnish-cache/issues/3221
.. _3171: https://github.com/varnishcache/varnish-cache/pull/3171

================================
Varnish Cache 6.0.6 (2020-02-04)
================================

* Fix an H2 locking bug during error handling. (3086_)

* Replace `python` with `python3` in all build scripts.

* Introduce `none` backends, which are empty backends that are always
  sick. This is documented in the ``vcl(7)`` manual page.

* Fix an assertion panic when labelling a VCL twice. (2834_)

* Fix semantics of VCL `auto` state management. (2836_)

* Fix a probe scheduling timeout sorting error. (3115_)

* Allow switching to the error state from the backend fetch and backend
  response states.  This makes it possible to execute `return (error)`
  from within the `vcl_backend_fetch()` and `vcl_backend_response()` VCL
  functions.

* Implement the `If-Range` header as specified in RFC 7233 section
  3.2. (RFC-7233-32_)

* Fix a denial of service vulnerability when using the proxy protocol
  version 2. (VSV00005_)

.. _3086: https://github.com/varnishcache/varnish-cache/issues/3086
.. _2834: https://github.com/varnishcache/varnish-cache/issues/2834
.. _2836: https://github.com/varnishcache/varnish-cache/issues/2836
.. _3115: https://github.com/varnishcache/varnish-cache/issues/3115
.. _RFC-7233-32: https://tools.ietf.org/html/rfc7233#section-3.2
.. _VSV00005: https://varnish-cache.org/security/VSV00005.html

================================
Varnish Cache 6.0.5 (2019-10-21)
================================

* Various H2 bug fixes and improvements have been back ported from the
  master branch. (2395_, 2572_, 2905_, 2923_, 2930_, 2931_, 2933_,
  2934_, 2937_, 2938_, 2967_)

* The idle timeout can now be set per session, by setting the
  `sess.timeout_idle` variable in VCL.

* A `param.reset` command has been added to `varnishadm`.

* Make waitinglist rushes propagate on streaming delivery. (2977_)

* Fix a problem where the ban lurker would skip objects. (3007_)

* Incremental VSM updates. With this change, added or removed VSM segments
  (ie varnishstat counters) will be done incrementally instead of complete
  republishments of the entire set of VSM segments. This reduces the load
  in the utilities (varnishncsa, varnishstat etc.) when there are frequent
  changes to the set.

* Optimize the VSM and VSC subsystems to handle large sets of counters
  more gracefully.

* Fix several resource leaks in libvarnishapi that would cause the
  utilities to incrementally go slower and use CPU cycles after many
  changes to the set of VSM segments.

* Fixed a VSM bug that would cause varnishlog like utilities to not
  produce log data. This could trigger when the varnish management process
  is running root, the cache worker as a non-privileged user, and the log
  utility run as the same user as the cache worker. This retires the
  VSM_NOPID environment variable.

* Fixed clearing of a state variable that could case an information leak
  (VSV00004_)

.. _2395: https://github.com/varnishcache/varnish-cache/issues/2395
.. _2572: https://github.com/varnishcache/varnish-cache/issues/2572
.. _2905: https://github.com/varnishcache/varnish-cache/issues/2905
.. _2923: https://github.com/varnishcache/varnish-cache/issues/2923
.. _2930: https://github.com/varnishcache/varnish-cache/issues/2930
.. _2931: https://github.com/varnishcache/varnish-cache/issues/2931
.. _2933: https://github.com/varnishcache/varnish-cache/pull/2933
.. _2934: https://github.com/varnishcache/varnish-cache/issues/2934
.. _2937: https://github.com/varnishcache/varnish-cache/issues/2937
.. _2938: https://github.com/varnishcache/varnish-cache/issues/2938
.. _2967: https://github.com/varnishcache/varnish-cache/issues/2967
.. _2977: https://github.com/varnishcache/varnish-cache/issues/2977
.. _3007: https://github.com/varnishcache/varnish-cache/issues/3007
.. _VSV00004: https://varnish-cache.org/security/VSV00004.html

================================
Varnish Cache 6.0.4 (2019-09-03)
================================

* Now ``std.ip()`` can optionally take a port number or service name
  argument. This is documented in the ``vmod_std(3)`` manual. (2993_)

* Permit subsequent conditional requests on 304. (2871_)

* Improved error messages from the VCL compiler on VMOD call argument
  missmatch. (2874_)

* Updated the builtin.vcl to use case-insensitive matching on
  Surrogate-Control and Cache-Control backend response headers.

* Ignore invalid Cache-Control time values containing trailing non-numeric
  characters.

* `varnishstat` now responds to the Home and End keys in interactive mode.

* Fix a compile issue when using newer builds of gcc. (2879_)

* Fix a VCL compilation assert when using very long VMOD object
  names. (2880_)

* Improved documentation on boolean types in VCL. (2846_)

* New VRT functions for handling STRANDS (split strings) in
  VMODs. `vmod_blob` now uses strands internally.

* New log tag `VCL_use` will show which VCL is in use during request
  handling.

* Fail VCL loading if a VMOD objects are left uninitialized. (2839_)

* Ensure that backend probes are executed in a timely manner. (2976_)

* Fixed issues related to HTTP/1 request parsing (VSV00003_)

.. _2993: https://github.com/varnishcache/varnish-cache/pull/2993
.. _2871: https://github.com/varnishcache/varnish-cache/issues/2871
.. _2874: https://github.com/varnishcache/varnish-cache/issues/2874
.. _2879: https://github.com/varnishcache/varnish-cache/issues/2879
.. _2880: https://github.com/varnishcache/varnish-cache/issues/2880
.. _2846: https://github.com/varnishcache/varnish-cache/issues/2846
.. _2839: https://github.com/varnishcache/varnish-cache/issues/2839
.. _2976: https://github.com/varnishcache/varnish-cache/issues/2976
.. _VSV00003: https://varnish-cache.org/security/VSV00003.html

================================
Varnish Cache 6.0.3 (2019-02-19)
================================

* Included ``vtree.h`` in the distribution for vmods and
  renamed the red/black tree macros from ``VRB_*`` to ``VRBT_*``
  to disambiguate from the acronym for Varnish Request Body.

* Added ``req.is_hitmiss`` and ``req.is_hitpass`` (2743_)

* Fix assinging <bool> == <bool> (2809_)

* Add error handling for STV_NewObject() (2831_)

* Fix VRT_fail for 'if'/'elseif' conditional expressions (2840_)

* Add VSL rate limiting (2837_)

  This adds rate limiting to varnishncsa and varnishlog.

* For ``varnishtest -L``, also keep VCL C source files.

* Make it possible to change ``varnishncsa`` update rate. (2741_)

* Tolerate null IP addresses for ACL matches.

* Many cache lookup optimizations.

* Display the VCL syntax during a panic.

* Update to the VCL diagrams to include hit-for-miss.

.. _2741: https://github.com/varnishcache/varnish-cache/pull/2741
.. _2743: https://github.com/varnishcache/varnish-cache/issues/2743
.. _2809: https://github.com/varnishcache/varnish-cache/issues/2809
.. _2831: https://github.com/varnishcache/varnish-cache/issues/2831
.. _2837: https://github.com/varnishcache/varnish-cache/pull/2837
.. _2840: https://github.com/varnishcache/varnish-cache/issues/2840

================================
Varnish Cache 6.0.2 (2018-11-07)
================================

* Fix and test objhead refcount for hit-for-pass (2654_, 2754_, 2760_)

* Allow a string argument to return(fail("Because!")); (2694_)

* Improve VCC error messages (2696_)

* Fix obj.hits in vcl_hit (2746_)

* Improvements to how PRIV_TASK and PRIV_TOP are initialized (2708_,
  2749_)

* fixed ``varnishhist`` display error (2780_)

* In ``Error: out of workspace`` log entries, the workspace name is
  now reported in lowercase

* Adjust code generator python tools to python 3 and prefer python 3
  over python 2 where available

* Clear the IMS object attribute when copying from a stale object
  (2763_)

* Implement and test ECMA-48 "REP" sequence to fix test case
  u00008.vtc on some newer platforms. (2668_)

* Don't mess with C-L when responding to HEAD (2744_)

* Align handling of STRINGS derived types (2745_)

* Fix some stats metrics (vsc) which were wrongly marked as _gauge_

* Varnishhist: Ignore non-positive values when accumulating (2773_)

* Fix production of VTC documentation (2777_)

* Fix ``varnishd -I`` (2782_)

* Fix ``varnishstat -f`` in curses mode (interactively, without
  ``-1``, 2787_)

* Changed the default of the ``thread_pool_watchdog`` parameter
  to 60 seconds to match the ``cli_timeout`` default

* Fix warmup/rampup of the shard director (2823_)

* Fix VRT_priv_task for calls from vcl_pipe {} (2820_)

* Fix vmod object constructor documentation in the ``vmodtool.py`` -
  generated RST files

* Vmod developers are advised that anything returned by a vmod
  function/method is assumed to be immutable. In other words, a vmod
  `must not` modify any data which was previously returned.

* ``Content-Length`` header is not rewritten in response to a HEAD
  request, allows responses to HEAD requests to be cached
  independently from GET responses.

* ``return(fail("mumble"))`` can have a string argument that is
  emitted by VCC as an error message if the VCL load fails due to the
  return. (2694_)

* Handle an out-of-workspace condition in HTTP/2 delivery more
  gracefully (2589_)

* Added a thread pool watchdog which will restart the worker process
  if scheduling tasks onto worker threads appears stuck. The new
  parameter ``thread_pool_watchdog`` configures it. (2418_, 2794_)

* Clarify and test object slimming for hfp+hfm. (2768_)

* Allow PRIORITY frames on closed streams (2775_)

* Hardening of the h2_frame_f callbacks (2781_)

* Added a JSON section to varnish-cli(7) (2783_)

* Improved varnish log client performance (2788_)

* Change nanosecond precision timestamps into microseconds (2792_)

* Only dlclose() Vmods after all "fini" processing (2800_)

* Fix VRT_priv_task for calls from vcl_pipe {} and test for it (2820_)

* Shard director: For warmup/rampup, only consider healthy backends
  (2823_)

.. _2418: https://github.com/varnishcache/varnish-cache/issues/2418
.. _2589: https://github.com/varnishcache/varnish-cache/issues/2589
.. _2654: https://github.com/varnishcache/varnish-cache/issues/2654
.. _2663: https://github.com/varnishcache/varnish-cache/pull/2663
.. _2668: https://github.com/varnishcache/varnish-cache/issues/2668
.. _2694: https://github.com/varnishcache/varnish-cache/issues/2694
.. _2696: https://github.com/varnishcache/varnish-cache/issues/2696
.. _2708: https://github.com/varnishcache/varnish-cache/issues/2708
.. _2713: https://github.com/varnishcache/varnish-cache/issues/2713
.. _2744: https://github.com/varnishcache/varnish-cache/pull/2744
.. _2745: https://github.com/varnishcache/varnish-cache/issues/2745
.. _2746: https://github.com/varnishcache/varnish-cache/issues/2746
.. _2749: https://github.com/varnishcache/varnish-cache/issues/2749
.. _2754: https://github.com/varnishcache/varnish-cache/issues/2754
.. _2760: https://github.com/varnishcache/varnish-cache/pull/2760
.. _2763: https://github.com/varnishcache/varnish-cache/issues/2763
.. _2768: https://github.com/varnishcache/varnish-cache/issues/2768
.. _2773: https://github.com/varnishcache/varnish-cache/issues/2773
.. _2775: https://github.com/varnishcache/varnish-cache/issues/2775
.. _2777: https://github.com/varnishcache/varnish-cache/issues/2777
.. _2780: https://github.com/varnishcache/varnish-cache/issues/2780
.. _2781: https://github.com/varnishcache/varnish-cache/pull/2781
.. _2782: https://github.com/varnishcache/varnish-cache/issues/2782
.. _2783: https://github.com/varnishcache/varnish-cache/issues/2783
.. _2787: https://github.com/varnishcache/varnish-cache/issues/2787
.. _2788: https://github.com/varnishcache/varnish-cache/issues/2788
.. _2792: https://github.com/varnishcache/varnish-cache/pull/2792
.. _2794: https://github.com/varnishcache/varnish-cache/issues/2794
.. _2800: https://github.com/varnishcache/varnish-cache/issues/2800
.. _2820: https://github.com/varnishcache/varnish-cache/issues/2820
.. _2823: https://github.com/varnishcache/varnish-cache/issues/2823


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
* 2275_ - If the client workspace is almost, but not quite exhaused, we may
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

* 1704_ - Reverted the docfix and made the fech_failed counter do
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

* Arithmetric on REAL data type in VCL is now possible.

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
- [varnishncsa] -w is now required when running deamonized.
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
* 1398_ - Fix NULL deref related to a backend we don't know any more.
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
  allowing browsers to diplay a progress bar.
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

-  An embarrasing typo in the new binary heap layout caused inflated
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

-  Change how backends are defined, to a constant structural defintion
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
   Fixed so we don't do that any more.

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
