..
	Copyright (c) 2010-2022 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. role:: ref(emphasis)

.. _varnish-params(7):

==============
varnish-params
==============

---------------------------
Varnish Run Time Parameters
---------------------------

:Manual section: 7

This document describes ``varnishd`` parameters that may be initialized
during startup with the ``-p`` option and later changed during run time
through the CLI with the ``param.set`` and ``param.reset`` commands.

Run Time Parameter Flags
------------------------

Runtime parameters are marked with shorthand flags to avoid repeating
the same text over and over in the table below. The meaning of the
flags are:

* `experimental`

  We have no solid information about good/bad/optimal values for this
  parameter. Feedback with experience and observations are most
  welcome.

* `delayed`

  This parameter can be changed on the fly, but will not take effect
  immediately.

* `restart`

  The worker process must be stopped and restarted, before this
  parameter takes effect.

* `reload`

  The VCL programs must be reloaded for this parameter to take effect.

* `wizard`

  Do not touch unless you *really* know what you're doing.

* `only_root`

  Only works if `varnishd` is running as root.

Default Value Exceptions on 32 bit Systems
------------------------------------------

Be aware that on 32 bit systems, certain default or maximum values are
reduced relative to the values listed below, in order to conserve VM
space:

* workspace_client: 24k
* workspace_backend: 20k
* http_resp_size: 8k
* http_req_size: 12k
* gzip_buffer: 4k
* vsl_buffer: 4k
* vsl_space: 1G (maximum)
* thread_pool_stack: 64k

Timeout Parameters
------------------

All timeout parameters follow a consistent naming scheme:

    <subject>_<event>_<type>

The subject may be one of the following:

- ``sess`` (client session)
- ``req`` (client request)
- ``resp`` (client response)
- ``pipe`` (client transaction turning into a pipe)
- ``bereq`` (backend request)
- ``beresp`` (backend response)
- ``backend`` (backend resource)
- ``cli`` (command line session)
- ``thread`` (worker thread)

Common events are:

- ``idle`` (waiting for data to be sent or received)
- ``send`` (complete delivery)
- ``start`` (first byte fetched)
- ``pool`` (time spent in a pool)

.. More common events for later:
.. - ``fetch`` (complete fetch)
.. - ``task`` (complete task)

Finally, there are two types of timeouts:

- ``timeout`` (definitive failure condition)
- ``interrupt`` (triggered to verify another condition)

List of Parameters
------------------

This text is produced from the same text you will find in the CLI if
you use the param.show command:

.. include:: ../include/params.rst

See also:
---------

* :ref:`varnishadm(1)`
* :ref:`varnishd(1)`
* :ref:`varnish-cli(7)`
