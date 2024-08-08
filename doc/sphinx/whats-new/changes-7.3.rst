.. _whatsnew_changes_7.3:

%%%%%%%%%%%%%%%%%%%%%%
Changes in Varnish 7.3
%%%%%%%%%%%%%%%%%%%%%%

For information about updating your current Varnish deployment to the
new version, see :ref:`whatsnew_upgrading_7.3`.

A more detailed and technical account of changes in Varnish, with
links to issues that have been fixed and pull requests that have been
merged, may be found in the `change log`_.

.. _change log: https://github.com/varnishcache/varnish-cache/blob/master/doc/changes.rst

varnishd
========

Parameters
~~~~~~~~~~

There is a new parameter ``transit_buffer`` disabled by default to limit the
amount of storage used for uncacheable responses. This is useful in situations
where slow clients may consume large but uncacheable objects, to prevent them
from filling up storage too fast at the expense of cacheable resources. When
transit buffer is enabled, a client request will effectively hold its backend
connection open until the client response delivery completes.

ESI processing changes
----------------------

Response status codes other than 200 and 204 are now considered errors for ESI
fragments.

Previously, any ``ESI:include`` object would be included, no matter what
the status of it were, 200, 503, didn't matter.

From now on, by default, only objects with 200 and 204 status will be
included and any other status code will fail the parent ESI request.

If objects with other status should be delivered, they should have
their status changed to 200 in VCL, for instance in ``sub
vcl_backend_error{}``, ``vcl_synth{}`` or ``vcl_deliver{}``.

If ``param.set feature +esi_include_onerror`` is used, and the
``<esi:include …>`` tag has a ``onerror="continue"`` attribute, any
and all ESI:include objects will be delivered, no matter what their
status might be, and not even a partial delivery of them will fail the
parent ESI request.  To be used with great caution.


Other changes in varnishd
~~~~~~~~~~~~~~~~~~~~~~~~~

In addition to classic Unix-domain sockets, Varnish now supports
abstract sockets. If the operating system supports them, as does any
fairly recent Linux kernel, abstract sockets can be specified using
the commonplace ``@`` notation for accept sockets, e.g.::

    varnishd -a @kandinsky

Weak ``Last-Modified`` headers whose timestamp lies within one second
of the corresponding ``Date`` header are no longer candidates for
revalidation. This means that a subsequent fetch will not, when a
stale object is available, include an ``If-Modified-Since`` header. A
weak ``Last-Modified`` header does not prevent ``Etag`` revalidation.

A cache hit on an object being streamed no longer prevents delivery of partial
responses (status code 206) to range requests.

Changes to VCL
==============

VCL variables
~~~~~~~~~~~~~

The variables ``req.xid``, ``bereq.xid`` and ``sess.xid`` are now integers
instead of strings, but should remain usable without a VCL change in a string
context.

Transit buffer can be controlled per fetch with the ``beresp.transit_buffer``
variable.

Other changes to VCL
~~~~~~~~~~~~~~~~~~~~

Backends have a new ``.via`` attribute optionally referencing another backend::

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

    backend miro {
      .path = "@miro";
    }

This is the same syntax as the ``varnishd -a`` command line option.

Probes have a new ``.expect_close`` attribute defaulting to ``true``, matching
the current behavior. Setting it to ``false`` will defer final checks until
after the probe times out.

varnishlog
==========

The in-memory and on-disk format of VSL records changed to allow 64bit
VXID numbers. The new binary format is **not compatible** with
previous versions, and log dumps performed with a previous Varnish
release are no longer readable from now on. Consequently, unused log
tags have been removed.

The VXID range is limited to ``VRT_INTEGER`` to fit in VCL the variables
``req.xid``, ``bereq.xid`` and ``sess.xid``.

A ``ReqStart`` record is emitted for bad requests, allowing ``varnishncsa`` to
find the client IP address.

varnishadm
==========

The ``debug.xid`` command generally used by ``varnishtest`` now sets
up the next VXID directly.

varnishtest
===========

It is now possible to send special keys NPAGE, PPAGE, HOME and END to a
process.

The ``-nolen`` option is implied for ``txreq`` and ``txresp`` when either
``Content-Length`` or ``Transfer-Encoding`` headers are present.

A new ``stream.peer_window`` variable similar to ``stream.window`` is
available for HTTP/2 checks.

Changes for developers and VMOD authors
=======================================

There is a new convenience macro ``WS_TASK_ALLOC_OBJ()`` to allocate objects
from the current tasks' workspace.

The ``NO_VXID`` macro can be used to explicitly log records outside of a
transaction.

Custom backend implementations are now in charge of printing headers, which
avoids duplicates when a custom implementation relied on ``http_*()`` that
would also log the headers being set up.

The ``VRT_new_backend*()`` functions take an additional backend argument, the
optional via backend. It cannot be a custom backend implementation, but it
can be a director resolving a native backend.

There is a new ``authority`` field for via backends in ``struct vrt_backend``.

There is a new ``exp_close`` field in ``struct vrt_backend_probe``.

Directors which take and hold references to other directors via
``VRT_Assign_Backend()`` (typically any director which has other
directors as backends) are now expected to implement the new
``.release`` callback of type ``void
vdi_release_f(VCL_BACKEND)``. This function is called by
``VRT_DelDirector()``. The implementation is expected drop any backend
references which the director holds (again using
``VRT_Assign_Backend()`` with ``NULL`` as the second argument).

*eof*
