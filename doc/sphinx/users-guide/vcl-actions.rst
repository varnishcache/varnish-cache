..
	Copyright (c) 2012-2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of licens

.. _user-guide-vcl_actions:

Actions
=======

Actions are used with the ``return(<action>)`` keyword, which returns
control from subroutines back to varnish. The action determines how
processing in varnish continues as shown in :ref:`reference-states`.

Common actions are documented here, while additional actions specific
to only one or some subroutines are documented in
:ref:`vcl-built-in-subs` as well as which action can be used from
which built in subroutine.

common actions for the client and backend side
----------------------------------------------

.. _fail:

``fail``
~~~~~~~~

    Transition to :ref:`vcl_synth` on the client side as for
    ``return(synth(503, "VCL Failed"))``, but with any request state
    changes undone as if ``std.rollback()`` was called and forcing a
    connection close.

    Intended for fatal errors, for which only minimal error handling is
    possible.

common actions for the client side
----------------------------------

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

common actions for the backend side
-----------------------------------

.. _abandon:

``abandon``
~~~~~~~~~~~

    Abandon the backend request. Unless the backend request was a
    background fetch, control is passed to :ref:`vcl_synth` on the
    client side with ``resp.status`` preset to 503.
