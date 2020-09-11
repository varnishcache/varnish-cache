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

Parameters
~~~~~~~~~~

A new ``vcc_acl_pedantic`` parameter will turn warnings into errors for the
case where an ACL entry includes a network prefix, but host bits aren't all
zeroes.

Other changes in varnishd
~~~~~~~~~~~~~~~~~~~~~~~~~

Some error messages improved in the VCL compiler.

Changes to VCL
==============

VCL variables
~~~~~~~~~~~~~

It is now possible to manually set a ``Connection: close`` header in
``beresp`` to signal that the backend connection shouldn't be recycled.
This might help dealing with backends that would under certain circumstances
have trouble managing their end of the connection, for example for certain
kinds of resources.

Care should be taken to preserve other headers listed in the connection
header::

    sub vcl_backend_response {
        if (beresp.backend == faulty_backend) {
            if (beresp.http.Connection) {
                set beresp.http.Connection += ", close";
            } else {
                set beresp.http.Connection = "close";
            }
        }
    }

**XXX new, deprecated or removed variables, or changed semantics**

Other changes to VCL
~~~~~~~~~~~~~~~~~~~~

VMODs
=====

A new ``std.blobread()`` function similar to ``std.fileread()`` was added to
work with binary files.

The shard director's ``.add_backend()`` method has a new optional ``weight``
parameter. An error when a backend is added or removed now fails the
transaction (or the ``vcl.load`` command in ``vcl_init``) but an invalid
weight does not result into a hard failure.

The shard director no longer outputs the (unused) ``canon_point`` property
in ``backend.list`` commands.

varnishlog
==========

The ``BackendReuse`` log record has been retired. It was inconsistently named
compared to other places like stat counters where we use the words reuse and
recycle (it should have been named ``BackendRecycle`` if anything).

The ``BackendOpen`` record can now tell whether the connection to the backend
was opened or reused from the pool, and the ``BackendClose`` record will tell
whether the connection was effectively closed or recycled into the pool.

varnishadm
==========

The ``backend.set_health`` command can be used to force a specific state
between sick and healthy or restore the automatic behavior, which depends on
the presence of a probe. While forcing a backend to be sick would prevent it
from being selected by a director, a straight selection of the backend from
VCL would still attempt a connection. This has been fixed, and the command's
documentation was clarified.

varnishstat
===========

A help screen is now available in interactive mode via the ``h`` key.

Again in interactive mode, verbosity is increased from the default value
during startup when the filtering of counters would otherwise display
nothing.

Filtering using the ``-f`` option is now deprecated in favor of ``-I`` and
``-X`` options that are treated in order. While still present, the ``-f``
option now also works in order instead of exclusive filters first and then
inclusive filters. It was also wrongly documented as inclusive first.

The JSON output slightly changed to more easily be consumed with programming
languages that may map JSON objects to types. See upgrade notes for more
details.

There are two new ``MAIN.beresp_uncacheable`` and ``MAIN.beresp_shortlived``
counters.

varnishtest
===========

The ``process -expect-text`` command will wait an order of magnitude longer
for the text to appear. It used to be too sensitive to any kind of timing
disruption.

Changes for developers and VMOD authors
=======================================

The workspace API saw a number of changes in anticipation of a future
inclusion in VRT. The deprecated ``WS_Reserve()`` function was finally
removed, the functions ``WS_ReserveSize()`` and ``WS_ReserveAll()`` were
introduced as a replacement.

On the topic of workspace reservation, the ``WS_Front()`` function is
now deprecated in favor of ``WS_Reservation()``. The two functions
behave similarly, but the latter ensures that it is only ever called
during a reservation. There was no legitimate reason to access the
workspace's front outside of a reservation.

In a scenario where a reservation is made in a part of the code, but
used somewhere else, it is possible to later query the size with the
new ``WS_ReservationSize()`` function.

The return value for ``WS_Printf()`` is now a constant string.

VMOD authors who would like to generate VCC files can now use the
``VARNISH_VMODS_GENERATED()`` macro from ``varnish.m4`` for autotools
builds.

**XXX changes concerning VRT, the public APIs, source code organization,
builds etc.**

*eof*
