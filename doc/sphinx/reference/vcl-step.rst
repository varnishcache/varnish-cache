..
	Copyright (c) 2013-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license


.. role:: ref(emphasis)

.. _vcl-step(7):

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
:ref:`vcl_actions` in the next section. Actions specific
to only one or some subroutines are documented in :ref:`vcl_steps`.

A default behavior is provided for all :ref:`reference-states` in the
:ref:`vcl-built-in-code` code.

.. _vcl_actions:

VCL Actions
===========

Actions are used with the ``return(<action>)`` keyword, which returns
control from subroutines back to varnish. The action determines how
processing in varnish continues as shown in :ref:`reference-states`.

Common actions are documented here, while additional actions specific
to only one or some subroutines are documented in the next section
:ref:`vcl_steps` as well as which action can be used from which built
in subroutine.

Common actions for the client and backend side
##############################################

.. _fail:

``fail``
~~~~~~~~

    Transition to :ref:`vcl_synth` on the client side as for
    ``return(synth(503, "VCL Failed"))``, but with any request state
    changes undone as if ``std.rollback()`` was called and forcing a
    connection close.

    Intended for fatal errors, for which only minimal error handling is
    possible.

Common actions for the client side
##################################

.. _synth:

``synth(status code, reason)``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    Transition to :ref:`vcl_synth` with ``resp.status`` and
    ``resp.reason`` being preset to the arguments of ``synth()``.

.. _pass:

``pass``
~~~~~~~~

    Switch to pass mode, making the current request not use the cache
    and not putting its response into it. Control will eventually pass to
    :ref:`vcl_pass`.

.. _pipe:

``pipe``
~~~~~~~~

    Switch to pipe mode. Control will eventually pass to
    :ref:`vcl_pipe`.

.. _connect:

``connect``
~~~~~~~~~~~

   Switch to connect mode. Control will eventually pass to
   :ref:`vcl_connect`

.. _restart:

``restart``
~~~~~~~~~~~

    Restart the transaction. Increases the ``req.restarts`` counter.

    If the number of restarts is higher than the *max_restarts*
    parameter, control is passed to :ref:`vcl_synth` as for
    ``return(synth(503, "Too many restarts"))``

    For a restart, all modifications to ``req`` attributes are
    preserved except for ``req.restarts`` and ``req.xid``, which need
    to change by design.

Common actions for the backend side
###################################

.. _abandon:

``abandon``
~~~~~~~~~~~

    Abandon the backend request. Unless the backend request was a
    background fetch, control is passed to :ref:`vcl_synth` on the
    client side with ``resp.status`` preset to 503.


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
