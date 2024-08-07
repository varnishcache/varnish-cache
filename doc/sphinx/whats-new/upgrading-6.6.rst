..
	Copyright 2021 UPLEX Nils Goroll Systemoptimierung
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _whatsnew_upgrading_6.6:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 6.6
%%%%%%%%%%%%%%%%%%%%%%%%

In general, this release should not come with relevant incompatibilities
to the previous release 6.5.

VCL should continue to work as before except when rather exotic,
partly unintended and/or undocumented features are used.

Header Validation
=================

Varnish now validates any headers set from VCL to contain only
characters allowed by RFC7230. A (runtime) VCL failure is triggered if
not. Such VCL failures, which result in ``503`` responses, should be
investigated. As a last resort, the ``validate_headers`` parameter can
be set to ``false`` to avoid these VCL failures.

BAN changes
===========

* The ``ban_cutoff`` parameter now refers to the overall length of the
  ban list, including completed bans, where before only non-completed
  ("active") bans were counted towards ``ban_cutoff``.

* The ``ban()`` VCL builtin is now deprecated and should be replaced
  with :ref:`whatsnew_changes_6.6_ban`

Accounting Changes
==================

Accounting statistics and Log records have changed. See
:ref:`whatsnew_changes_6.6_accounting` for details.

VSL changes
===========

The ``-c`` option of log utilities no longer includes ESI requests. A
new ``-E`` option is now available for ESI requests and it implies ``-c``
too. This brings all log utilities on par with ``varnishncsa`` where the
``-E`` option was initially introduced.

If you use ``-c`` to collect both client and ESI requests, you should
use ``-E`` instead. If you use ``-c`` and a VSL query to exclude ESI
requests, the query should no longer be needed.

VMOD ``cookie`` functions
=========================

The regular expression arguments taken by various functions from the
``cookie`` VMOD now need to be literal. See
:ref:`whatsnew_changes_6.6_cookie` for details.

Other VCL Changes
=================

* The ``resp.proto`` variable is now read-only as it should have been
  for long, like the other ``*.proto`` variables.

  Changing the protocol is an error and should not be required.

* Trying to use ``std.rollback()`` from ``vcl_pipe`` now results in
  VCL failure.

* ``return(retry)`` from ``vcl_backend_error {}`` now correctly resets
  ``beresp.status`` and ``beresp.reason``.

Changes to VMODs
================

Many VMODs will need minor adjustments to work with this release. See
:ref:`whatsnew_changes_6.6_vmod` for details.

*eof*
