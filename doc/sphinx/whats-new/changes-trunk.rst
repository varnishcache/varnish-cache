**Note: This is a working document for a future release, with running
updates for changes in the development branch. For changes in the
released versions of Varnish, see:** :ref:`whats-new-index`

.. _whatsnew_changes_CURRENT:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Changes in Varnish **$NEXT_RELEASE**
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

For information about updating your current Varnish deployment to the
new version, see :ref:`whatsnew_upgrading_CURRENT`.

A more detailed and technical account of changes in Varnish, with
links to issues that have been fixed and pull requests that have been
merged, may be found in the `change log`_.

.. _change log: https://github.com/varnishcache/varnish-cache/blob/master/doc/changes.rst

varnishd
========

Cache lookups have undergone a number of optimizations, among them to
reduce lock contention, and to shorten and simplify the critical
section of lookup code.

We have added a "watchdog" for thread pools that will panic the worker
process, causing it to restart, if scheduling tasks onto worker
threads appears to be deadlocking. The length of time until the panic
is set by the :ref:`ref_param_thread_pool_watchdog` parameter. If this
happens, it probably means that thread pools are too small, and you
should consider increasing the parameters
:ref:`ref_param_thread_pool_min`, :ref:`ref_param_thread_pool_max`
and/or :ref:`ref_param_thread_pools`.

Parameters
~~~~~~~~~~

Some parameters that have been long deprecated are now retired. Now
you must use these parameters:

* :ref:`ref_param_vsl_reclen` (in place of ``shm_reclen``)

* :ref:`ref_param_vcl_path` (in place of ``vcl_dir``)

* :ref:`ref_param_vmod_path` (in place of ``vmod_dir``)

Added :ref:`ref_param_thread_pool_watchdog`, see above.

The :ref:`ref_param_debug` parameter now has a flag ``vcl_keep``. When
the flag is turned on, C sources and shared object libraries that were
generated from VCL sources are retained in the Varnish working
directory (see the notes about ``varnishtest`` below).

Other changes in varnishd
~~~~~~~~~~~~~~~~~~~~~~~~~

The VCL syntax version is now displayed in a panic message, as 41 for
VCL 4.1 and 40 for VCL 4.0.

Changes to VCL
==============

VCL variables
~~~~~~~~~~~~~

Added ``req.is_hitmiss`` and ``req.is_hitpass``, see :ref:`vcl(7)`.

Other changes to VCL
~~~~~~~~~~~~~~~~~~~~

When the ``.path`` field of a backend declaration is used to define a
Unix domain socket as the backend address, and the socket file does
not exist or is not accessible at VCL load time, then a warning is
issued, but the VCL load is allowed to continue. Previously, the load
would fail in that case. This makes it easier to start the peer
component listening at the socket, or set the socket's permissions,
after starting Varnish or loading VCL. If the socket still cannot be
accessed when a fetch is attempted, then the fetch fails.

``return(miss)`` from ``vcl_hit{}`` did never work as intended for the
common case (it actually turned into a pass), so we now removed it and
changed the ``builtin.vcl``.

VMODs
=====

Added the function :ref:`vmod_directors.lookup`, only for use in
``vcl_init`` or ``vcl_fini``.

The type-conversion functions in :ref:`vmod_std(3)` have been reworked
to make them more flexible and easier to use:
:ref:`vmod_std.duration`, :ref:`vmod_std.bytes`,
:ref:`vmod_std.integer`, :ref:`vmod_std.real` and :ref:`vmod_std.time`
now also accept suitable numeral or quantitative arguments.

These type-conversion functions should be fully backwards compatible,
but the following differences should be noted:

* The *fallback* is not required and more. A conversion failure in the
  absense of a *fallback* argument will now trigger a VCL failure.

* A VCL failure will also be triggered if no or more than one argument
  (plus optional *fallback*) is given.

* Conversion functions now only ever truncate if necessary (instead of
  rounding).

* :ref:`vmod_std.round` has been added for explicit rounding.

The functions :ref:`vmod_std.real2integer`, :ref:`vmod_std.real2time`,
:ref:`vmod_std.time2integer` and :ref:`vmod_std.time2real` are
superseded by these changed and should thus be replaced by the above
mentioned conversion functions. They will be removed in a future
version of Varnish.

varnishlog(1), varnishncsa(1) and vsl(7)
========================================

The performance of bundled log readers, including ``varnishlog`` and
``varnishncsa`` (and any tool using the internal VUT interface for
Varnish utilities) has been improved. They continue reading log
contents in bulk as long as more contents are known to be available,
not stopping as frequently (and unnecessarily) to check the status of
the shared memory mapping.

``varnishlog`` and ``varnishncsa`` now have the ``-R`` command-line
option for rate-limiting, to limit the number of log transactions read
per unit time.  This can make it less likely for log reads to fall
behind and fail with overrun errors under heavy loads. See
:ref:`varnishlog(1)` and :ref:`varnishncsa(1)` for details.

Timing information is now uniformly reported in the log with
microsecond precision.  This affects the tags ``ExpKill`` and
``ExpRearm`` (previously with nanosecond precision).

varnishadm(1) and varnish-cli(7)
================================

.. _whatsnew_changes_cli_json:

JSON output
~~~~~~~~~~~

JSON responses, requested with the ``-j`` option, are now possible for
the following commands (see :ref:`varnish-cli(7)`):

* ``status -j``
* ``vcl.list -j``
* ``param.show -j``
* ``ban.list -j``
* ``storage.list -j``
* ``panic.show -j``

The ``-j`` option was already available for ``backend.list``, ``ping``
and ``help`` in previous versions.

For automated parsing of CLI responses (``varnishadm`` output), we
recommend the use of JSON format.

``param.reset <param>``
~~~~~~~~~~~~~~~~~~~~~~~

Added the command ``param.reset`` to reset a parameter's value to its
default, see :ref:`varnish-cli(7)`.

.. _whatsnew_changes_vcl_list_backend_list:

Listing backends
~~~~~~~~~~~~~~~~

``backend.list`` has grown an additional column, the output has
changed and fields are now of dynamic width:

* The ``Admin`` column now accurately states ``probe`` only if a
  backend has some means of dynamically determining health state.

* The ``Probe`` column has been changed to display ``X/Y``, where:

  * Integer ``X`` is the number of good probes in the most recent
    window; or if the backend in question is a director, the number of
    healthy backends accessed via the director or any other
    director-specific metric.

  * Integer ``Y`` is the window in which the threshold for overall
    health of the backend is defined (from the ``.window`` field of a
    probe, see :ref:`vcl(7)`); or in the case of a director, the total
    number of backends accessed via the director or any other
    director-specific metric.

  If there is no probe or the director does not provide details,
  ``0/0`` is output.

* The ``Health`` column has been added to contain the dynamic (probe)
  health state and the format has been unified to just ``healthy`` or
  ``sick``.

  If there is no probe, ``Health`` is always given as
  ``healthy``. Notice that the administrative health as shown in the
  ``Admin`` column has precedence.

In the ``probe_message`` field of ``backend.list -j`` output, the
``Probe`` and ``Health`` columns appears as the array ``[X, Y,
health]``.

See :ref:`varnish-cli(7)` for details.

Listing VCLs
~~~~~~~~~~~~

The non-JSON output of ``vcl.list`` has been changed:

* The ``state`` and ``temperature`` fields appear in separate columns
  (previously combined in one column).

* The optional column showing the relationships between labels and VCL
  configurations (when labels are in use) has been separated into two
  columns.

See :ref:`varnish-cli(7)` for details. In the JSON output for
``vcl.list -j``, this information appears in separate fields.

The width of columns in ``backend.list`` and ``vcl.list`` output
(non-JSON) is now dynamic, to fit the width of the terminal window.

Banning by expiration parameters
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Bans may now be defined with respect to ``obj.ttl``, ``obj.age``,
``obj.grace`` and ``obj.keep``, referring to the expiration and age
properties of the cached object. A ban expression may also be defined
with one of the comparison operators ``<``, ``<=``, ``>`` and ``>=``;
these may only be used with one of the new duration variables for
bans. Duration constants (such as ``5m`` for five minutes of ``3h``
for three hours) may be used in the ``<arg>`` position against which
these objects are compared in a ban expression.

``obj.ttl`` and ``obj.age`` are evaluated with respect to the time at
which the ban was defined, while ``obj.grace`` and ``obj.keep`` are
evaluated as the grace or keep time assigned to the object. So to issue
a ban for objects whose TTL expires more than 5 hours from now and
whose keep parameter is greater than 3 hours, use this expression::

  obj.ttl > 5h && obj.keep > 3h

See :ref:`vcl(7)` and :ref:`users-guide-purging` for details.

varnishstat(1) and varnish-counters(7)
======================================

Added the ``ws_*_overflow`` and ``client_resp_500`` counters to better
diagnose workspace overflow issues, see :ref:`varnish-counters(7)`.

In curses mode, :ref:`varnishstat(1)` now allows use of the ``+`` and
``-`` keys to increase or decrease the refresh rate of the curses
window.

varnishtest
===========

When :ref:`varnishtest(1)` is invoked with either of the ``-L`` or
``-l`` options to retain the temporary directory after tests, the
``vcl_keep`` flag for the :ref:`ref_param_debug` is switched on (see
`Parameters`_ above). This means that C sources and shared objects
generated from VCL can also be inspected after a test. By default, the
temporary directory is deleted after each test.

Changes for developers and VMOD authors
=======================================

Python tools that generate code now prefer python 3 over python 2,
when availabale.

Directors
~~~~~~~~~

The director API has been changed slightly: The most relevant design
change is that the ``healthy`` callback now is the only means to
determine a director's health state dynamically, the ``sick`` member
of ``struct director`` has been removed. Consequently,
``VRT_SetHealth()`` has been removed and ``VRT_SetChanged()`` added to
update the last health state change time.

Presence of the ``healthy`` callback now also signifies if the
director is considered to have a *probe* with respect to the CLI.

The signature of the ``list`` callback has been changed to reflect the
retirement of the undocumented ``backend.list -v`` parameter and to
add a ``VRT_CTX``.

*eof*
