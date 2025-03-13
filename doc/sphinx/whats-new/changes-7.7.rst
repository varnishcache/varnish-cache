.. _whatsnew_changes_7.7:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Changes in Varnish-Cache 7.7
%%%%%%%%%%%%%%%%%%%%%%%%%%%%

For information about updating your current Varnish deployment to the new
version, see :ref:`whatsnew_upgrading_7.7`.

**NOTE**: In this Varnish-Cache release, we changed how timestamps are taken for
the http2 protocol, which could look like a performance regression, but is not.
See :ref:`whatsnew_changes_7.7_h2_timestamps`.

A more detailed and technical account of changes in Varnish-Cache, with links to
issues that have been fixed and pull requests that have been merged, may be
found in the `change log`_.

.. _change log: https://github.com/varnishcache/varnish-cache/blob/master/doc/changes.rst

varnishd
========

Parameters
~~~~~~~~~~

The bitfield parameters ``debug``, ``experimental``, ``feature``,
``vcc_feature`` and ``vsl_mask`` now consistently support the special values
``all`` and ``none``. The output format of ``varnishadm param.show`` has been
adjusted to always output the parameter value relative to either ``all`` or
``none`` in a format which would also be accepted by ``varnishadm param.set``
and the ``varnishd -p`` option.

The new ``http_req_overflow_status`` parameter now allows to optionally send a
response with a status between ``400`` and ``499`` (inclusive) if a request
exceeds ``http_req_size``.  The default of ``0`` keeps the existing behavior
to just close the connection in this case.

The new ``ban_any_variant`` parameter allows to configure the maximum number
of possibly non matching variants evaluated against the ban list during
lookup. The default value of 10000 avoids excessive time spent for ban checks
during lookups, which could cause noticeable delays for cases with a very high
number of bans and/or variants (in the 1000s).

Setting ``ban_any_variant`` to ``0`` changes the behavior of the lookup-time
ban check to only consider matching objects for tests against the ban list,
which can be considered a bugfix, depending on the exact interpretation of the
semantics of ban expressions with regards to variants. ``0`` will become the
new default in a future release of Varnish-Cache.

Jails
~~~~~

The ``linux`` jail gained control of transparent huge pages (THP) settings: The
``transparent_hugepage`` suboption can be set to ``ignore`` to do nothing,
``enable`` to enable THP (actually, disable the disable), ``disable`` to disable
THP or ``try-disable`` to try do disable, but not emit an error if disabling
fails. ``try-disable`` is the default.

Error handling from the jail subsystem has been streamlined to avoid some
confusing and/or contradictory error messages as well as turn assertion failures
into error messages.

Other changes in varnishd
~~~~~~~~~~~~~~~~~~~~~~~~~

An issue has been fixed which could cause a crash when ``varnishd`` receives
an invalid ``Content-Range`` header from a backend.

Changes to VCL
==============

VCL variables
~~~~~~~~~~~~~

VCL now supports ``unset req.grace`` and ``unset req.ttl`` to reset the
respective variables to the "no effect" value, which is also the default.

The scope of VCL variables ``req.is_hitmiss`` and ``req.is_hitpass`` is now
restricted to ``vcl_miss, vcl_deliver, vcl_pass, vcl_synth`` and ``vcl_pass,
vcl_deliver, vcl_synth``, respectively.

The ``Content-Length`` header is now consistently removed after ``unset
bereq.body`` on the backend side.

Other changes to VCL
~~~~~~~~~~~~~~~~~~~~

Behavior of the VCL ``include`` statement with the ``+glob`` option has been
clarified to not search directories in ``vcl_path``. Using ``+glob`` includes
with a relative path that does not start with "./" will now result in a VCL
compile failure.

Validation of the ``PROXY2`` ``PP2_TYPE_AUTHORITY`` TLV sent with ``.via``
backends has been corrected: IP addresses are no longer accepted as an
authority and port numbers are automatically removed.

``return (fail(...))`` can now take strings returned from a vmod.

Generic Logging (VSL)
=====================

affecting ``varnishlog``, ``varnishncsa`` and ``varnishtop``:

.. _whatsnew_changes_7.7_h2_timestamps:

http2 related timestamps
~~~~~~~~~~~~~~~~~~~~~~~~

Timestamps for http/2 requests have been corrected and made similar to how they
are taken for http/1.

For http/1, the start time, internally called "t_first", is taken as soon as any
part of the request (headers) is received. Previously, http/2 took it later,
possibly much later if long header lines were involved. http/2 now takes it the
same way as http/1 when the first bit of the first HEADERS frame of the request
arrives.

Timing behavior for http/1 and http/2 is different and can not be directly
compared. But with this change, the ``Timestamp`` VSL records for http/2 now at
least reflect reality better.

**NOTE** that after upgrading Varnish-Cache, processing and response times for
http/2 will now be reported as worse than before the upgrade, potentially *much*
worse. This is **NOT** a performance regression, but rather due to the corrected
timestamps, which arguably were wrong for http/2.

http2 logging
~~~~~~~~~~~~~

For http/2, normal client behavior like timeouts or closed connection was logged
with a ``SessError`` tag and ``ENHANCE_YOUR_CALM`` in additional ``Debug`` log
records. This behavior was misleading and has been corrected.

http/2 error detail reporting in ``Debug`` log records has been clarified:
Connection errors are now prefixed with ``H2CE_``, and stream errors with
``H2SE_``, respectively.

http/2 ``BogoHeader`` log records now contain the first offending byte value in
hex.

Interactive mode in varnishstat, varnishtop and varnishhist
===========================================================

Handling of curses errors in the interactive mode of ``varnishstat``,
``varnishtop`` and ``varnishhist`` has been streamlined and one wrong assertion
has been fixed, which could cause a crash with certain terminal types as set
through the ``TERM`` environment variable.

varnishncsa
===========

The ``hitmiss`` and ``hitpass`` handling indicators have been added to the
``Varnish:handling`` format of ``varnishncsa``.

``varnishncsa`` now handles headers unset and changed from VCL more
consistently: request headers are logged as they were received from the client
and as they were sent to the backend, while response headers are logged as they
were sent to the client and as they were received from the backend.

varnishstat
===========

Pressing the ``0`` key in ``varnishstat`` interactive (curses) mode now resets
averages.

The backend ``happy`` VSC bitfield is now set to all ones for backends with no
configured probe.

varnishtest
===========

``varnishtest`` can now send arbitrary http/2 settings frames and arbitrary
PROXY2 tlvs.

``varnishtest`` has been changed to always set a ``VARNISH_DEFAULT_N``
environment variable to ensure that ``varnish`` invoked from ``varnishtest``
always has a valid workdir.

Changes for developers and VMOD authors
=======================================

``miniobj.h``: Helper macros ``SIZEOF_FLEX_OBJ()`` and ``ALLOC_FLEX_OBJ()`` have
been added to facilitate use of structs with flexible array members.

The acceptor code has been refactored for basic support of pluggable acceptors.

Two fields have been added to the VMOD data registered with varnish-cache:

- ``vcs`` for Version Control System is intended as an identifier from the
  source code management system, e.g. the git revision, to identify the exact
  source code which was used to build a VMOD binary.

- ``version`` is intended as a more user friendly identifier as to which
  version of a vmod a binary represents.

The panic output and the ``debug.vmod`` CLI command output now contain these
identifiers.

Where supported by the compiler and linker, the ``vcs`` identifier is also
reachable via the ``.vmod_vcs`` section of the vmod shared object ELF file and
can be extracted, for example, using ``readelf -p.vmod_vcs <file>``

To set the version, ``vmodtool.py`` now accepts a ``$Version`` stanza in vmod
vcc files. If ``$Version`` is not present, an attempt is made to extract
``PACKAGE_STRING`` from an automake ``Makefile``, otherwise ``NOVERSION`` is
used as the version identifier.

A new facility has been added allowing transport delivery functions to disembark
the worker thread which had been handling a request's VCL code during delivery
by returning ``VTR_D_DISEMBARK`` from the ``vtr_deliver_f`` function.

This will enable future optimizations to make transport protocol code more
efficient.

To enable this facility, a new request processing step ``finish`` has been added
once delivery is complete.

*eof*
