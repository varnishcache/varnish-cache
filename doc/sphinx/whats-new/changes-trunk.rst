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

**XXX changes in -p parameters**

Other changes in varnishd
~~~~~~~~~~~~~~~~~~~~~~~~~

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

**XXX changes in the bundled VMODs**

varnishlog
==========

**XXX changes concerning varnishlog(1) and/or vsl(7)**

varnishadm
==========

**XXX changes concerning varnishadm(1) and/or varnish-cli(7)**

varnishstat
===========

**XXX changes concerning varnishstat(1) and/or varnish-counters(7)**

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
