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

There is a new parameter ``transit_buffer`` disabled by default to limit the
amount of storage used for uncacheable responses. This is useful in situations
where slow clients may consume large but uncacheable objects, to prevent them
from filling up storage too fast at the expense of cacheable resources. When
transit buffer is enabled, a client request will effectively hold its backend
connection open until the client response delivery completes.

Other changes in varnishd
~~~~~~~~~~~~~~~~~~~~~~~~~

In addition to classic Unix-domain sockets, abstract sockets can now be used
on Linux. Instead of an absolute path, the syntax ``-a @name`` can be used to
bind the abstract socket called ``name``.

Weak ``Last-Modified`` headers are no longer candidates for revalidation. This
means that a subsequent fetch will not, when such a stale object is available,
include an ``If-Modified-Since`` header. A weak ``Last-Modified`` header does
not prevent ``Etag`` revalidation.

A cache hit on an object being streamed no longer prevents delivery of partial
responses (status code 206) to range requests.

Response status codes other than 200 and 204 are now considered errors for ESI
fragments. The default behavior was changed, errors are no longer delivered by
default. The feature flag ``esi_include_onerror`` can be raised to allow a
backend to specify whether to continue.

Changes to VCL
==============

VCL variables
~~~~~~~~~~~~~

**XXX new, deprecated or removed variables, or changed semantics**

The variables ``req.xid``, ``bereq.xid`` and ``sess.xid`` are now integers
instead of strings, but should remain usable without a VCL change in a string
context.

Transit buffer can be controlled per fetch with the ``beresp.transit_buffer``
variable.

Other changes to VCL
~~~~~~~~~~~~~~~~~~~~

Backends have a new ``.via`` attribute referencing another backend::

    backend detour {
        .host = "...";
    }

    backend destination {
        .host = "...";
        .via = detour;
    }

Attempting a connection for ``destination`` connects to ``detour`` with a
PROXYv2 protocol header targeting ``destination``'s address. Optionally, the
``destination`` backend could use the other new ``.authority`` attribute to
define an authority TLV in the PROXYv2 header.

Backends can connect to abstract sockets on linux::

    backend abstract {
        .path = "@name";
    }

This is the same syntax as the ``varnishd -a`` command line option.

Probes have a new ``.expect_close`` attribute defaulting to ``true``, matching
the current behavior. Setting it to ``false`` will defer final checks until
after the probe times out.

VMODs
=====

**XXX changes in the bundled VMODs**

varnishlog
==========

**XXX changes concerning varnishlog(1) and/or vsl(7)**

The in-memory and on-disk format of VSL records changed to allow 64bit VXID
numbers. The new binary format is not compatible with previous versions, and
log dumps performed with a previous Varnish release are no longer readable
from now on.

The VXID range is limited to ``VRT_INTEGER`` to fit in VCL the variables
``req.xid``, ``bereq.xid`` and ``sess.xid``.

A ``ReqStart`` record is emitted for bad requests, allowing ``varnishncsa`` to
find the client IP address.

varnishadm
==========

**XXX changes concerning varnishadm(1) and/or varnish-cli(7)**

The ``debug.xid`` command generally used by ``varnishtest`` used to set up the
current VXID. As the intent usually is to set up the next VXID, this forced to
set an off-by-one value. To simplify its usage it now sets up the next VXID
directly.

varnishstat
===========

**XXX changes concerning varnishstat(1) and/or varnish-counters(7)**

varnishtest
===========

**XXX changes concerning varnishtest(1) and/or vtc(7)**

It is now possible to send special keys NPAGE, PPAGE, HOME and END to a
process.

The ``-nolen`` option is implied for ``txreq`` and ``txresp`` when either
``Content-Length`` or ``Transfer-Encoding`` headers are present.

A new ``stream.peer_window`` variable similar to ``stream.window`` is
available for HTTP/2 checks.

Changes for developers and VMOD authors
=======================================

**XXX changes concerning VRT, the public APIs, source code organization,
builds etc.**

There is a new convenience macro ``WS_TASK_ALLOC_OBJ()`` to allocate objects
from the current tasks' workspace.

The ``NO_VXID`` macro can be used to explicitly log records outside of a
transaction.

Custom backend implementations are now in charge of printing headers, which
avoids duplicates when a custom implementation relied on ``http_*()`` that
would also log the headers being set up.

*eof*
