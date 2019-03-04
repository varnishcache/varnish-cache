**Note: This is a working document for a future release, with running
updates for changes in the development branch. For changes in the
released versions of Varnish, see:** :ref:`whats-new-index`

.. _whatsnew_upgrading_CURRENT:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish **$NEXT_RELEASE**
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

**XXX: how to upgrade from previous deployments to this
version. Limited to work that has to be done for an upgrade, new
features are listed in "Changes". Explicitly mention what does *not*
have to be changed, especially in VCL. May include, but is not limited
to:**

* Elements of VCL that have been removed or are deprecated, or whose
  semantics have changed.

* -p parameters that have been removed or are deprecated, or whose
  semantics have changed.

* Changes in the CLI.

* Changes in the output or interpretation of stats or the log, including
  changes affecting varnishncsa/-hist/-top.

* Changes that may be necessary in VTCs or in the use of varnishtest.

* Changes in public APIs that may require changes in VMODs or VAPI/VUT
  clients.

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


``return(miss)`` from ``vcl_hit{}`` is now removed. Options to
implement similar functionality are:

* a vmod using the new *catflap* mechanism

* ``return (restart)`` from ``vcl_hit{}`` and ``set
  req.hash_always_miss = true;`` in ``vcl_recv{}`` for the restart.

Runtime parameters
==================

Some varnishd ``-p`` parameters that have been deprecated for some
time have been removed. If you haven't changed them yet, you have to
now.  These are:

* ``shm_reclen`` -- use :ref:`ref_param_vsl_reclen` instead

* ``vcl_dir`` -- use :ref:`ref_param_vcl_path` instead

* ``vmod_dir`` -- use :ref:`ref_param_vmod_path` instead

varnishadm and the CLI
======================

The output formats of the ``vcl.list`` and ``backend.list`` commands
have changed, see :ref:`whatsnew_changes_vcl_list_backend_list` and
:ref:`varnish-cli(7)` for details. In non-JSON mode, the width of
columns in ``backend.list`` and ``vcl.list`` output is now dynamic, to
fit the width of the terminal window.

The ``-j`` option for JSON output has been added to a number of
commands, see :ref:`whatsnew_changes_cli_json` and
:ref:`varnish-cli(7)`. We recommend the use of JSON format for
automated parsing of CLI responses (:ref:`varnishadm(1)` output).

*eof*
