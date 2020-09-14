**Note: This is a working document for a future release, with running
updates for changes in the development branch. For changes in the
released versions of Varnish, see:** :ref:`whats-new-index`

.. _whatsnew_upgrading_CURRENT:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish **$NEXT_RELEASE**
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

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

The filter option ``-f`` is now deprecated in favor of the ``-I`` and
``-X`` options for field inclusions and exclusions, respectively. Tools
using ``varnishstat`` should prepare for future removal and be changed
accordingly.

VSL
===

If you need to build VSL queries that depend on ``BackendReuse`` you can
now rely on ``BackendClose``, for example::

    varnishlog -q 'BackendReuse[2] ~ www'

The new query would be::

    varnishlog -q 'BackendClose[2] ~ www and BackendClose[3] eq recycle'

Changes for developers and VMOD authors
=======================================

VSB
~~~

VSB support for dynamic vs. static allocations has been changed and
code using VSBs will need to be adjusted, see
:ref:`whatsnew_changes_CURRENT_libvarnish`.

It should be noted that the VSB itself and the string buffer must be either
both dynamic or both static. It is no longer possible for example to have
a static ``struct`` with a dynamic buffer with the new API.

Workspace API
~~~~~~~~~~~~~

VMODs using the Workspace API might need minor adjustments, see
:ref:`whatsnew_changes_CURRENT_workspace`.

In general, accessing any field of ``struct ws`` is strongly discouraged
and if the workspace API doesn't satisfy all your needs please bring
that to our attention.

VSC
~~~

The ``'f'`` argument for ``VSC_Arg()`` is now deprecated as mentioned in
the above note on `varnishstat`_ and :ref:`whatsnew_changes_CURRENT_vsc`.

Otherwise you can use the ``'I'`` ans ``'X'`` arguments to respectively
include or exclude counters, they work in a first-match fashion. Since
``'f'`` is now emulated using the new arguments, its filtering behavior
slightly changed from exclusions first to first match.

If like ``varnishstat`` in curses mode, you have a utility that always
needs some counters to be present the ``'R'`` argument takes a glob of
required fields. Such counters are not affected by filtering from other
``VSC_Arg()`` arguments.

*eof*
