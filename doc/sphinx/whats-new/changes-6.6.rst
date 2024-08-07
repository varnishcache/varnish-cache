..
	Copyright 2021 UPLEX Nils Goroll Systemoptimierung
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _whatsnew_changes_6.6:

%%%%%%%%%%%%%%%%%%%%%%
Changes in Varnish 6.6
%%%%%%%%%%%%%%%%%%%%%%

For information about updating your current Varnish deployment to the
new version, see :ref:`whatsnew_upgrading_6.6`.

A more detailed and technical account of changes in Varnish, with
links to issues that have been fixed and pull requests that have been
merged, may be found in the `change log`_.

.. _change log: https://github.com/varnishcache/varnish-cache/blob/master/doc/changes.rst

varnishd
========

Arguments
~~~~~~~~~

* ``varnishd`` now supports the ``-b none`` argument to start with
  only the builtin VCL and no backend at all.

Parameters
~~~~~~~~~~

* The ``validate_headers`` parameter has been added to control
  `header validation <whatsnew_changes_6.6_header_validation_>`_.

* The ``ban_cutoff`` parameter now refers to the overall length of the
  ban list, including completed bans, where before only non-completed
  ("active") bans were counted towards ``ban_cutoff``.

* The ``vary_notice`` parameter has been added to control the
  threshold for the new `Vary Notice
  <whatsnew_changes_6.6_vary_notice_>`_.

``feature`` Flags
~~~~~~~~~~~~~~~~~

* The ``busy_stats_rate`` feature flag has been added to ensure
  statistics updates (as configured using the ``thread_stats_rate``
  parameter) even in scenarios where worker threads never run out
  of tasks and may remain forever busy.

.. _whatsnew_changes_6.6_accounting:

Accounting
~~~~~~~~~~

Body bytes accounting has been fixed to always represent the number of
body bytes moved on the wire, exclusive of protocol-specific overhead
like HTTP/1 chunked encoding or HTTP/2 framing.

This change affects counters like

- ``MAIN.s_req_bodybytes``,

- ``MAIN.s_resp_bodybytes``,

- ``VBE.*.*.bereq_bodybytes`` and

- ``VBE.*.*.beresp_bodybytes``

as well as the VSL records

- ``ReqAcct``,

- ``PipeAcct`` and

- ``BereqAcct``.

.. _whatsnew_changes_6.6_sc_close:

Session Close Reasons
~~~~~~~~~~~~~~~~~~~~~

The connection close reason has been fixed to properly report
``SC_RESP_CLOSE`` / ``resp_close`` where previously only
``SC_REQ_CLOSE`` / ``req_close`` was reported.

For failing PROXY connections, ``SessClose`` now provides more
detailed information on the cause of the failure.

The session close reason logging/statistics for HTTP/2 connections
have been improved.

.. _whatsnew_changes_6.6_vary_notice:

Vary Notice
~~~~~~~~~~~

A log (VSL) ``Notice`` record is now emitted whenever more than
``vary_notice`` variants are encountered in the cache for a specific
hash. The new ``vary_notice`` parameter defaults to 10.

Changes to VCL
==============

.. _whatsnew_changes_6.6_header_validation:

Header Validation
~~~~~~~~~~~~~~~~~

Unless the new ``validate_headers`` feature is disabled, all newly set
headers are now validated to contain only characters allowed by
RFC7230. A (runtime) VCL failure is triggered if not.

VCL variables
~~~~~~~~~~~~~

* The ``client.identity`` variable is now accessible on the backend
  side.

* The variables ``bereq.is_hitpass`` and ``bereq.is_hitmiss`` have
  been added to the backend side matching ``req.is_hitpass`` and
  ``req.is_hitmiss`` on the client side.

* The ``bereq.xid`` variable is now also available in ``vcl_pipe {}``

* The ``resp.proto`` variable is now read-only as it should have been
  for long, like the other ``*.proto`` variables.

Other changes to VCL
~~~~~~~~~~~~~~~~~~~~

* Long strings in VCL can now also be denoted using ``""" ... """`` in
  addition to the existing ``{" ... "}``.

* The ``ban()`` builtin is now deprecated and should be replaced with
  `std.ban() <whatsnew_changes_6.6_ban_>`_.

* Trying to use ``std.rollback()`` from ``vcl_pipe`` now results in
  VCL failure.

* The modulus operator ``%`` has been added to VCL.

* ``return(retry)`` from ``vcl_backend_error {}`` now correctly resets
  ``beresp.status`` and ``beresp.reason``.

* The builtin VCL has been reworked: VCL code has been split into
  small subroutines, which custom VCL can prepend custom code to.

  This allows for better integration of custom VCL and the built-in
  VCL and better reuse.

VMODs
=====

``directors.shard()``
~~~~~~~~~~~~~~~~~~~~~

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

``std.set_ip_tos()``
~~~~~~~~~~~~~~~~~~~~

The ``set_ip_tos()`` function from the bundled ``std`` vmod now sets
the IPv6 Traffic Class (TCLASS) when used on an IPv6 connection.

.. _whatsnew_changes_6.6_ban:

``std.ban()`` and ``std.ban_error()``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``std.ban()`` and ``std.ban_error()`` functions have been added to
the ``std`` vmod, allowing VCL to check for ban errors. A typical
usage pattern with the new interface is::

  if (std.ban(...)) {
    return(synth(200, "Ban added"));
  } else {
    return(synth(400, std.ban_error()));
  }

.. _whatsnew_changes_6.6_cookie:

``cookie`` functions
~~~~~~~~~~~~~~~~~~~~

The ``filter_re``, ``keep_re`` and ``get_re`` functions from the
bundled ``cookie`` vmod have been changed to take the ``VCL_REGEX``
type. This implies that their regular expression arguments now need to
be literal, whereas before they could be taken from some other
variable or function returning ``VCL_STRING``.

Note that these functions never actually handled *dynamic* regexen,
the string passed with the first call was compiled to a regex, which
was then used for the lifetime of the respective VCL.


varnishlog
==========

* See `Accounting <whatsnew_changes_6.6_accounting_>`_ for changes
  to accounting-related VSL records.

* See `Session Close Reasons <whatsnew_changes_6.6_sc_close_>`_
  for a change affecting ``SessClose``.

* Three new ``Timestamp`` VSL records have been added to backend
  request processing:

  - The ``Process`` timestamp after ``return(deliver)`` or
    ``return(pass(x))`` from ``vcl_backend_response``,

  - the ``Fetch`` timestamp before a backend connection is requested
    and

  - the ``Connected`` timestamp when a connection to a regular backend
    (VBE) is established, or when a recycled connection was selected for
    reuse.

* The ``FetchError`` log message ``Timed out reusing backend
  connection`` has been renamed to ``first byte timeout (reused
  connection)`` to clarify that it is emit for effectively the same
  reason as ``first byte timeout``.

* ``ExpKill`` log (VSL) records are now masked by default. See the
  ``vsl_mask`` parameter.

* Comparisons of numbers in VSL queries have been improved to match
  better the behavior which is likely expected by users who have not
  read the documentation in all detail.

* See `Vary Notice <whatsnew_changes_6.6_vary_notice_>`_ for
  information on a newly added ``Notice`` log (VSL) record.

varnishncsa
===========

* The ``%{X}T`` format has been added to ``varnishncsa``, which
  generalizes ``%D`` and ``%T``, but also support milliseconds
  (``ms``) output.

* The ``varnishncsa`` ``-E`` argument to show ESI requests has been
  changed to imply ``-c`` (client mode). This behavior is now shared
  by all log utilities, and ``-c`` no longer includes ESI requests.


varnishadm
==========

* The ``vcl.discard`` CLI command can now be used to discard more than
  one VCL with a single command, which succeeds only if all given VCLs
  could be discarded (atomic behavior).

* The ``vcl.discard`` CLI command now supports glob patterns for vcl names.

* The ``vcl.deps`` CLI command has been added to output dependencies
  between VCLs (because of labels and ``return(vcl)`` statements).

* ``varnishadm`` now has the ``-p`` option to disable readline support
  for use in scripts and as a generic CLI connector.

varnishstat
===========

* See `Accounting <whatsnew_changes_6.6_accounting_>`_ for changes
  to accounting-related counters.

* See `Session Close Reasons <whatsnew_changes_6.6_sc_close_>`_
  for a change affecting ``MAIN.sc_*`` counters.

* The ``MAIN.esi_req`` counter has been added as a statistic of the
  number of ESI sub requests created.

* The ``MAIN.s_bgfetch`` counter has been added as a statistic on the
  number of background fetches issued.

.. _whatsnew_changes_6.6_varnishstat_raw:

* ``varnishstat`` now avoids display errors of gauges which previously
  could underflow to negative values, being displayed as extremely
  high positive values.

  The ``-r`` option and the ``r`` key binding have been added to
  return to the previous behavior. When raw mode is active in
  ``varnishstat`` interactive (curses) mode, the word ``RAW`` is
  displayed at the right hand side in the lower status line.

varnishtest
===========

Various improvements have been made to the ``varnishtest`` facility:

- the ``loop`` keyword now works everywhere

- HTTP/2 logging has been improved

- Default HTTP/2 parameters have been tweaked

- Varnish listen address information is now available by default in
  the macros ``${vNAME_addr}``, ``${vNAME_port}`` and
  ``${vNAME_sock}``. Macros by the names ``${vNAME_SOCKET_*}`` contain
  the address information for each listen socket as created with the
  ``-a`` argument to ``varnishd``.

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

Other Changes to Varnish Utilities
==================================

All varnish tools using the VUT library utilities for argument
processing now support the ``--optstring`` argument to return a string
suitable for use with ``getopts`` from shell scripts.

.. _whatsnew_changes_6.6_vmod:

Developer: Changes for VMOD authors
===================================

VMOD/VCL interface
~~~~~~~~~~~~~~~~~~

* The ``VCL_REGEX`` data type is now supported for VMODs, allowing
  them to use regular expression literals checked and compiled by the
  VCL compiler infrastructure.

  Consequently, the ``VRT_re_init()`` and ``VRT_re_fini()`` functions
  have been removed, because they are not required and their use was
  probably wrong anyway.

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

* ``vmodtool.py`` has been improved to simplify Makefiles when many
  VMODs are built in a single directory.

General API
~~~~~~~~~~~

* ``VRT_ValidHdr()`` has been added for VMODs to conduct the same
  check as the `whatsnew_changes_6.6_header_validation`_ feature,
  for example when headers are set by VMODs using the ``cache_http.c``
  Functions like ``http_ForceHeader()`` from untrusted input.

* Client and backend finite state machine internals (``enum req_step``
  and ``enum fetch_step``) have been removed from ``cache.h``.

* The ``verrno.h`` header file has been removed and merged into
  ``vas.h``

* The ``pdiff()`` function declaration has been moved from ``cache.h``
  to ``vas.h``.

VSA
~~~

* The ``VSA_getsockname()`` and ``VSA_getpeername()`` functions have
  been added to get address information of file descriptors.

Private Pointers
~~~~~~~~~~~~~~~~

* The interface for private pointers in VMODs has been changed:

  - The ``free`` pointer in ``struct vmod_priv`` has been replaced
    with a pointer to ``struct vmod_priv_methods``, to where the
    pointer to the former free callback has been moved as the ``fini``
    member.

  - The former free callback type has been renamed from
    ``vmod_priv_free_f`` to ``vmod_priv_fini_f`` and as gained a
    ``VRT_CTX`` argument

* The ``VRT_priv_task_get()`` and ``VRT_priv_top_get()`` functions
  have been added to VRT to allow vmods to retrieve existing
  ``PRIV_TASK`` / ``PRIV_TOP`` private pointers without creating any.

Backends
~~~~~~~~

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

Filters (VDP/VFP)
~~~~~~~~~~~~~~~~~

* Many filter (VDP/VFP) related signatures have been changed:

  - ``vdp_init_f``

  - ``vdp_fini_f``

  - ``vdp_bytes_f``

  - ``VDP_bytes()``

  as well as ``struct vdp_entry`` and ``struct vdp_ctx``

  ``VFP_Push()`` and ``VDP_Push()`` are no longer intended for VMOD
  use and have been removed from the API.

* The VDP code is now more strict about ``VDP_END``, which must be
  sent down the filter chain at most once. Care should be taken to
  send ``VDP_END`` together with the last payload bytes whenever
  possible.

Stevedore API
~~~~~~~~~~~~~

* The stevedore API has been changed:

  - ``OBJ_ITER_FINAL`` has been renamed to ``OBJ_ITER_END``

  - ``ObjExtend()`` signature has been changed to also cover the
    ``ObjTrimStore()`` use case and

  - ``ObjTrimStore()`` has been removed.

Developer: Changes for Authors of Varnish Utilities
===================================================

libvarnishapi
~~~~~~~~~~~~~

* The ``VSC_IsRaw()`` function has been added to ``libvarnishapi`` to
  query if a gauge is being returned raw or adjusted (see
  `varnishstat -r option <whatsnew_changes_6.6_varnishstat_raw_>`_).

*eof*
