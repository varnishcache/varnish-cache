..
	Copyright (c) 2020 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of licens

.. _whatsnew_changes_6.4:

%%%%%%%%%%%%%%%%%%%%%%%%
Changes in Varnish 6.4.0
%%%%%%%%%%%%%%%%%%%%%%%%

For information about updating your current Varnish deployment to the
new version, see :ref:`whatsnew_upgrading_6.4`.

A more detailed and technical account of changes in Varnish, with
links to issues that have been fixed and pull requests that have been
merged, may be found in the `change log`_.

.. _change log: https://github.com/varnishcache/varnish-cache/blob/master/doc/changes.rst

varnishd
========

bugs
~~~~

Numerous bugs have been fixed.

Generic Parameter Handling
~~~~~~~~~~~~~~~~~~~~~~~~~~

Some parameters have dependencies and those are better documented now. For
example :ref:`ref_param_thread_pool_min` can't be increased above
:ref:`ref_param_thread_pool_max`, which is now indicated as such in the
manual.

On a running Varnish instance the ``param.show`` command will display the
actual minimum or maximum, but an attempt to ``param.set`` a parameter above
or below its dynamic maximum or minimum will mention the failure's cause in
the error message::

    varnish> param.show thread_pool_reserve
    200
    thread_pool_reserve
            Value is: 0 [threads] (default)
            Maximum is: 95

            [...]

    varnish> param.show thread_pool_min
    200
    thread_pool_min
            Value is: 100 [threads] (default)
            Maximum is: 5000

            [...]

    varnish> param.set thread_pool_reserve 100
    106
    Must be no more than 95 (95% of thread_pool_min)

    (attempting to set param 'thread_pool_reserve' to '100')

Expect further improvements in future releases.

Parameters
~~~~~~~~~~

* Raised the minimum for the :ref:`ref_param_vcl_cooldown` parameter
  to 1 second.

Changes in behavior
~~~~~~~~~~~~~~~~~~~

* The ``if-range`` header is now handled, allowing clients to conditionally
  request a range based on a date or an ETag.

* Output VCC warnings also for VCLs loaded via the ``varnishd -f``
  option

Changes to VCL
==============

* New syntax for "no backend"::

      backend dummy none;

      sub vcl_recv {
          set req.backend_hint = dummy;
      }

  It can be used whenever a backend is needed for syntactical
  reasons. The ``none`` backend will fail any attempt to use it.
  The other purpose is to avoid the declaration of a dummy backend
  when one is not needed: for example an active VCL only passing
  requests to other VCLs with the ``return (vcl(...))`` syntax or
  setups relying on dynamic backends from a VMOD.

* ``std.rollback(bereq)`` is now safe to use, see :ref:`vmod_std(3)`
  for details.

* Deliberately closing backend requests through ``return(abandon)``,
  ``return(fail)`` or ``return(error)`` is no longer accounted as a
  fetch failure.

* Numerical expressions can now be negative or negated as in
  ``set resp.http.ok = -std.integer("-200");``.

* The ``+=`` operator is now available for headers and response bodies::

      set resp.http.header += "string";

VCL variables
~~~~~~~~~~~~~

* Add more vcl control over timeouts with the ``sess.timeout_linger``,
  ``sess.send_timeout`` and ``sess.idle_send_timeout`` variables
  corresponding the parameters by the same names.

VMODs
=====

* Imported :ref:`vmod_cookie(3)` from `varnish_modules`_

  The previously deprecated function ``cookie.filter_except()`` has
  been removed during import. It was replaced by ``cookie.keep()``

varnishlog
==========

* A ``Notice`` VSL tag has been added.

* Log records can safely have empty fields or fields containing blanks
  if they are delimited by "double quotes". This was applied to
  ``SessError`` and ``Backend_health``.

varnishadm
==========

* New ``pid`` command in the Varnish CLI, to get the master and optionally
  cache process PIDs, for example from ``varnishadm``.

varnishstat
===========

* Add vi-style CTRL-f / CTRL-b for page down/up to interactive
  ``varnishstat``.

* The ``MAIN.sess_drop`` counter is gone.

* Added ``rx_close_idle`` counter for separate accounting when
  ``sess.timeout_idle`` / :ref:`ref_param_timeout_idle` is reached.

* ``sess.send_timeout`` / :ref:`ref_param_send_timeout` being reached
  is no longer reported as ``MAIN.sc_rem_close``, but as
  ``MAIN.sc_tx_error``.

Changes for developers and VMOD authors
=======================================

General
~~~~~~~

* New configure switch: ``--with-unwind``. Alpine linux appears to offer a
  ``libexecinfo`` implementation that crashes when called by Varnish, this
  offers the alternative of using ``libunwind`` instead.

* The option ``varnishtest -W`` is gone, the same can be achieved with
  ``varnishtest -p debug=+witness``. A ``witness.sh`` script is available
  in the source tree to generate a graphviz dot file and detect potential
  lock cycles from the test logs.

* Introduced ``struct reqtop`` to hold information on the ESI top request
  and ``PRIV_TOP``.

* New or improved Coccinelle semantic patches that may be useful for
  VMOD or utilities authors.

* Added ``VSLs()`` and ``VSLbs()`` functions for logging ``STRANDS`` to
  VSL.

* Added ``WS_VSB_new()`` / ``WS_VSB_finish()`` for VSBs on workspaces.

* added ``v_dont_optimize`` attribute macro to instruct compilers
  (only gcc as of this release) to not optimize a function.

* Added ``VSB_tofile()`` to ``libvarnishapi``.

VMODs
~~~~~

* It is now possible for VMOD authors to customize the connection pooling
  of a dynamic backend. A hash is now computed to determine uniqueness and
  a backend declaration can contribute arbitrary data to influence the pool.

* ``VRB_Iterate()`` signature has changed.

* ``VRT_fail()`` now also works from director code.

* ``body_status`` and ``req_body_status`` have been collapsed into one
  type. In particular, the ``REQ_BODY_*`` enums now have been replaced
  with ``BS_*``.

* Added ``VRT_AllocStrandsWS()`` as a utility function to allocate
  STRANDS on a workspace.

*eof*

.. _varnish_modules: https://github.com/varnish/varnish-modules
