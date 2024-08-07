..
	Copyright (c) 2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _whatsnew_upgrading_2019_03:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 6.2
%%%%%%%%%%%%%%%%%%%%%%%%

.. _whatsnew_upgrading_vcl_2019_03:

VCL
===

VCL programs for Varnish 6.1 can be expected to run without changes in
the new version.

A VCL load will now issue a warning, but does not fail as previously,
if a backend declaration uses the ``.path`` field to specify a Unix
domain socket, but the socket file does not exist or is not accessible
at VCL load time. This makes it possible to start the peer component
listening at the socket, or set its permissions, after Varnish starts
or the VCL is loaded. Backend fetches fail if the socket is not
accessible by the time the fetch is attempted.

``return(miss)`` from ``vcl_hit{}`` is now removed. An option for
implementing similar functionality is:

* ``return (restart)`` from ``vcl_hit{}``

* in ``vcl_recv{}`` for the restart (when ``req.restarts`` has
  increased), ``set req.hash_always_miss = true;``.

.. _whatsnew_upgrading_params_2019_03:

Runtime parameters
==================

Some varnishd ``-p`` parameters that have been deprecated for some
time have been removed. If you haven't changed them yet, you have to
now.  These are:

* ``shm_reclen`` -- use :ref:`ref_param_vsl_reclen` instead

* ``vcl_dir`` -- use :ref:`ref_param_vcl_path` instead

* ``vmod_dir`` -- use :ref:`ref_param_vmod_path` instead

The default value of :ref:`ref_param_thread_pool_stack` has been
increased from 48k to 56k on 64-bit platforms and to 52k on 32-bit
platforms. See the discussion under
:ref:`whatsnew_changes_params_2019_03` in
:ref:`whatsnew_changes_2019_03` for details.

.. _whatsnew_upgrading_std_conversion_2019_03:

Type conversion functions in VMOD std
=====================================

The existing type-conversion functions in :ref:`vmod_std(3)` have been
reworked to make them more flexible and easier to use. These functions
now also accept suitable numeral or quantitative arguments.

* :ref:`std.duration()`
* :ref:`std.bytes()`
* :ref:`std.integer()`
* :ref:`std.real()`
* :ref:`std.time()`

These type-conversion functions should be fully backwards compatible,
but the following differences should be noted:

* The *fallback* is not required anymore. A conversion failure in the
  absence of a *fallback* argument will now trigger a VCL failure.

* A VCL failure will also be triggered if no or more than one argument
  (plus optional *fallback*) is given.

* Conversion functions now only ever truncate if necessary (instead of
  rounding).

* :ref:`std.round()` has been added for explicit rounding.

The following functions are deprecated and should be replaced by the
new conversion functions:

* :ref:`std.real2integer()`
* :ref:`std.real2time()`
* :ref:`std.time2integer()`
* :ref:`std.time2real()`

They will be removed in a future version of Varnish.

varnishadm and the CLI
======================

The ``-j`` option for JSON output has been added to a number of
commands, see :ref:`whatsnew_changes_cli_json` in
:ref:`whatsnew_changes_2019_03` and :ref:`varnish-cli(7)`. We
recommend the use of JSON format for automated parsing of CLI
responses (:ref:`varnishadm(1)` output).

.. _whatsnew_upgrading_backend_list_2019_03:

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
``Probe`` and ``Health`` columns appear as the array ``[X, Y,
health]``.

See :ref:`varnish-cli(7)` for details.

.. _whatsnew_upgrading_vcl_list_2019_03:

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

For developers and authors of VMODs and API clients
===================================================

Python 3.4 or later is now required to build Varnish, or use scripts
installed along with Varnish, such as ``vmodtool.py`` to build VMODs
or other Varnish artifacts. Python 2 is no longer supported, and this
support will likely be dropped in a future 6.0 LTS release too.

The VRT API has been bumped to version 9.0. Changes include:

* Functions in the API have been added, and others removed.

* The ``VCL_BLOB`` type is now implemented as ``struct vrt_blob``.

* The ``req_bodybytes`` field of ``struct req`` has been removed, and
  should now be accessed as an object core attribute.

See ``vrt.h``, the `change log`_ and
:ref:`whatsnew_changes_director_api_2019_03` in
:ref:`whatsnew_changes_2019_03` for details.

.. _change log: https://github.com/varnishcache/varnish-cache/blob/master/doc/changes.rst

The vmodtool has been changed significantly to avoid name clashes in
the C identifiers declared in ``vcc_if.h``. This may necessitate
changing names in your VMOD code. To facilitate renaming, ``vcc_if.h``
defines macros for prepending the vmod prefix, and for naming enums
and argument structs. For details, see the `change log`_, and examine
the contents of ``vcc_if.h`` after generation.

Going forward, we will adhere to the principle that data returned by
VMOD methods and functions are immutable. This is now enforced in some
places by use of the ``const`` modifier. A VMOD is free to do as it
sees fit within its own implementation, but if you attempt to change
something returned by another VMOD, the results are undefined.

*eof*
