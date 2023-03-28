..
	Copyright (c) 2013-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license


.. role:: ref(emphasis)

.. _vcl-steps(7):

=========
VCL-steps
=========

--------------------
Built-in subroutines
--------------------

:Manual section: 7


DESCRIPTION
===========

Various built-in subroutines are called during processing of client
and backend requests as well as upon ``vcl.load`` and ``vcl.discard``.

See :ref:`reference-states` for a detailed graphical overview of the
states and how they relate to core code functions and VCL subroutines.

Built-in subroutines always terminate with a ``return (<action>)``,
where ``<action>`` determines how processing continues in the request
processing state machine.

The behaviour of actions is identical or at least similar across
subroutines, so differences are only documented where relevant.

Common actions are documented in
:ref:`user-guide-vcl_actions`. Actions specific to only one or some
subroutines are documented herein.

A default behavior is provided for all :ref:`reference-states` in the
:ref:`vcl-built-in-code` code.


.. include:: vcl_step.rst



SEE ALSO
========

* :ref:`varnishd(1)`
* :ref:`vcl(7)`

COPYRIGHT
=========

This document is licensed under the same license as Varnish
itself. See LICENSE for details.

* Copyright (c) 2006 Verdens Gang AS
* Copyright (c) 2006-2021 Varnish Software AS
