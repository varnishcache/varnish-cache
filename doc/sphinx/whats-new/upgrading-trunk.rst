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

varnishstat
===========

The JSON output (``-j`` option) changed to avoid having the ``timestamp``
field mixed with the counters fields. As such the schema version was bumped
from 0 to 1, and a ``version`` top-level field was added to keep track of
future schema changes. Counters are in a new ``counters`` top-level field.

Before::

  {
    "timestamp": "YYYY-mm-ddTHH:MM:SS",
    "MGT.uptime": {
      ...
    },
    ...
  }

After::

  {
    "version": 1,
    "timestamp": "YYYY-mm-ddTHH:MM:SS",
    "counters": {
      "MGT.uptime": {
        ...
      },
      ...
    }
  }

VSL
===

If you need to build VSL queries that depend on ``BackendReuse`` you can
now rely on ``BackendClose``, for example::

    varnishlog -q 'BackendReuse[2] ~ www'

The new query would be::

    varnishlog -q 'BackendClose[2] ~ www and BackendClose[3] eq recycle'

*eof*
