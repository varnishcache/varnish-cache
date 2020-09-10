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

**XXX new, deprecated or removed variables, or changed semantics**

Other changes to VCL
~~~~~~~~~~~~~~~~~~~~

VMODs
=====

**XXX changes in the bundled VMODs**

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

**XXX changes concerning varnishadm(1) and/or varnish-cli(7)**

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

**XXX changes concerning varnishtest(1) and/or vtc(7)**

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

**XXX changes concerning VRT, the public APIs, source code organization,
builds etc.**

*eof*
