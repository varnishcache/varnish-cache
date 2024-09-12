.. _whatsnew_changes_CURRENT:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Changes in Varnish **${NEXT_RELEASE}**
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

For information about updating your current Varnish deployment to the
new version, see :ref:`whatsnew_upgrading_CURRENT`.

A more detailed and technical account of changes in Varnish, with
links to issues that have been fixed and pull requests that have been
merged, may be found in the `change log`_.

.. _change log: https://github.com/varnishcache/varnish-cache/blob/master/doc/changes.rst

Changes applying to most varnish-cache programs
===============================================

The environment variable ``VARNISH_DEFAULT_N`` now provides the default "varnish
name" / "workdir" as otherwise specified by he ``-n`` argument to ``varnishd``
and ``varnish*`` utilities except ``varnishtest``.

varnishd
========

A new ``linux`` jail has been added (configured via the ``-j`` argument) which is
now the default on Linux. For now, it is almost identical to the ``unix`` jail
with one :ref:`whatsnew_upgrading_CURRENT_linux_jail` added.

The port of a *listen_endpoint* given with the ``-a`` argument to ``varnishd``
can now also be a numerical port range like ``80-89``, besides the existing
options of port number (e.g. ``80``) and service name (e.g. ``http``). With a
port range, Varnish will accept connections on all ports within the range.

.. _whatsnew_changes_CURRENT_connq:

Backend connection queuing
~~~~~~~~~~~~~~~~~~~~~~~~~~

A feature has been added to instruct backend tasks to queue if the backend has
reached its ``max_connections``. This allows tasks to wait for a connection to
become available rather than immediately fail. This feature must be enabled
through new global parameters or individual backend properties:

* ``backend_wait_timeout`` sets the amount of time a task will wait.
* ``backend_wait_limit`` sets the maximum number of tasks that can wait.

These parameters can also be set for individual backends using the
``wait_timeout`` and ``wait_limit`` properties.

Tasks waiting on a backend going sick (either explicitly via the
``backend.set_health`` command or implicitly through the probe) fail
immediately.

Global VSC counters have been added under ``MAIN``:

* ``backend_wait`` counts tasks which waited in queue for a connection.
* ``backend_wait_fail`` counts tasks which waited in queue but failed because
  ``wait_timeout`` was reached or the backend went sick.

Parameters
~~~~~~~~~~

The ``backend_wait_timeout`` and ``backend_wait_limit`` parameters have been
added, see :ref:`whatsnew_changes_CURRENT_connq` above for details.

The size of the buffer to hold panic messages is now tunable through the new
``panic_buffer`` parameter.

Changes to VCL
==============

The ``wait_timeout`` and ``wait_limit`` backend properties have been added, see
:ref:`whatsnew_changes_CURRENT_connq` above for details.

For backends using the ``.via`` attribute to connect through a *proxy*, the
``connect_timeout``, ``first_byte_timeout`` and ``between_bytes_timeout``
attributes are now inherited from *proxy* unless explicitly given.

varnishlog
==========

Additional ``SessError`` VSL events are now generated for various HTTP/2
protocol errors. Some HTTP/2 log events have been changed from ``Debug`` and
``Error`` to ``SessError``.

varnishstat
===========

VSC counters for waiters have been added:

* ``conns`` to count waits on idle connections
* ``remclose`` to count idle connections closed by the peer
* ``timeout`` to count idle connections which timed out in the waiter
* ``action`` to count idle connections which resulted in a read

These can be found under ``WAITER.<poolname>.``.

The ``MAIN.backend_wait`` and ``MAIN.backend_wait_fail`` counters have been
added, see :ref:`whatsnew_changes_CURRENT_connq` above for details.

varnishtest
===========

``varnishtest`` now supports the ``shutdown`` command corresponding to the
``shutdown(2)`` standard C library call.

Changes for developers and VMOD authors
=======================================

.. _whatsnew_changes_CURRENT_VDP:

VDP filter API changes
~~~~~~~~~~~~~~~~~~~~~~

The Varnish Delivery Processor (VDP) filter API has been generalized to also
accommodate future use for backend request bodies:

``VDP_Init()`` gained a ``struct busyobj *`` argument for use of VDPs on the
backend side, which is mutually exclusive with the existing ``struct req *``
argument (one of the two needs to be ``NULL``). ``VDP_Init()`` also gained an
``intmax_t *`` pointer, which needs to point to the known content length of the
body data or ``-1`` for "unknown length". Filters can change this value.

``struct vdp_ctx`` lost the ``req`` member, but gained ``struct objcore *oc``,
``struct http *hp`` and ``intmax_t *clen`` members. The rationale here is that a
VDP should be concerned mainly with transforming body data (for which ``clen``
is relevant) and optionally changing (from the ``vdp_init_f``) the headers sent
before the body data, for which ``hp`` is intended. Some VDPs also work directly
on a ``struct objcore *``, so ``oc`` is provided to the first VDP in the chain
only.

Generic VDPs should specifically not access the request or be concerned with the
object.

Yet special purpose VDPs still can take from ``VRT_CTX`` whatever references
they need in the ``vdp_init_f`` and store them in their private data.

Consequent to what as been explained above, ``vdp_init_f`` lost its ``struct
objcore *`` argument.

VDPs with no ``vdp_bytes_f`` function are now supported if the ``vdp_init_f``
returns a value greater than zero to signify that the filter is not to be added
to the chain. This is useful to support VDPs which only need to work on headers.

.. _whatsnew_changes_CURRENT_Obj:

Object API changes
~~~~~~~~~~~~~~~~~~

The ``ObjWaitExtend()`` Object API function gained a ``statep`` argument to
optionally return the busy object state consistent with the current extension.
A ``NULL`` value may be passed if the caller does not require it.

Other changes relevant for developers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``VSS_resolver_range()`` as been added to ``libvarnish`` to implement resolution
of port ranges.

The implementation of the ``transit_buffer`` has now been made the
responsibility of storage engines.

*eof*
