..
	Copyright (c) 2015-2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _ref-writing-a-director:

%%%%%%%%%%%%%%%%%%
Writing a Director
%%%%%%%%%%%%%%%%%%

Varnish already provides a set of general-purpose directors, and since Varnish
4, it is bundled in the built-in :ref:`vmod_directors(3)`. Writing a director
boils down to writing a VMOD, using the proper data structures and APIs. Not
only can you write your own director if none of the built-ins fit your needs,
but since Varnish 4.1 you can even write your own backends.

Backends can be categorized as such:

- static: native backends declared in VCL
- dynamic: native backends created by VMODs
- custom: backends created and fully managed by VMODs


Backends vs Directors
=====================

The intuitive classification for backend and director is an endpoint for the
former and a loadbalancer for the latter, but the actual implementation is a bit
more subtle. VMODs can accept backend arguments and return backends in VCL (see
:ref:`ref-vmod-vcl-c-types`), but the underlying C type is ``struct director``
aka the ``VCL_BACKEND`` typedef.
Under the hood director is a generic concept, and a backend is a kind of
director.

The line between the two is somewhat blurry at this point, let's look at some
code instead::

    // VRT interface from vrt.h

    struct vdi_methods {
        unsigned                        magic;
    #define VDI_METHODS_MAGIC           0x4ec0c4bb
        const char                      *type;
        vdi_http1pipe_f                 *http1pipe;
        vdi_healthy_f                   *healthy;
        vdi_resolve_f                   *resolve;
        vdi_gethdrs_f                   *gethdrs;
        vdi_getip_f                     *getip;
        vdi_finish_f                    *finish;
        vdi_event_f                     *event;
        vdi_destroy_f                   *destroy;
        vdi_panic_f                     *panic;
        vdi_list_f                      *list;
    };

    struct director {
        unsigned                        magic;
    #define DIRECTOR_MAGIC              0x3336351d
        void                            *priv;
        char                            *vcl_name;
        struct vcldir                   *vdir;
    };

A director can be summed up as:

- being of a specific ``type`` with a set of operations which is
  identical for all instances of that particular type

- some instance specific attributes such as a ``vcl_name``
  and ``type``\ -specific private data

The difference between a *load balancing* director and a *backend*
director is mainly the functions they will implement.

The fundamental steps towards a director implementation are:

- implement the required functions

- fill a ``struct vdi_methods`` with the name of your director type
  and your function pointers

  Existence of a ``healthy`` callback signifies that the director has
  some means of dynamically determining its health state.

- in your constructor or other initialization routine, allocate and
  initialize your director-specific configuration state (aka private
  data) and call ``VRT_AddDirector()`` with your ``struct
  vdi_methods``, the pointer to your state and a printf format for the
  name of your director instance

- implement methods or functions returning ``VCL_BACKEND``

- in your destructor or other finalizer, call ``VRT_DelDirector()``

For forwards compatibility, it is strongly recommended for the last
step not to destroy the actual director private state, but rather
implement and declare in ``struct vdi_methods`` a ``destroy``
callback.

While vmods can implement functions returning directors,
:ref:`ref-vmod-vcl-c-objects` are usually a more natural
representation with vmod object instances being or referring to the
director private data.

Load Balancing Directors
========================

As in :ref:`vmod_directors(3)`, you can write directors that will group
backends sharing the same role, and pick them according to a strategy. If you
need more than the built-in strategies (round-robin, hash, ...), even though
they can be stacked, it is always possible to write your own.

In this case you simply need to implement the ``resolve`` function for the
director. Directors are walked until a leaf director is found. A leaf director
doesn't have a ``resolve`` function and is used to actually make the backend
request, just like the backends you declare in VCL.


Dynamic Backends
================

If you want to speak HTTP/1 over TCP or UDS, but for some reason VCL
does not fit the bill, you can instead reuse the whole backend
facility. It allows you for instance to add and remove backends
on-demand without the need to reload your
VCL. You can then leverage your provisioning system.

Consider the following snippet::

    backend default {
        .host = "localhost";
    }

The VCL compiler turns this declaration into a ``struct
vrt_backend``. When the VCL is loaded, Varnish calls
``VRT_new_backend`` (or rather ``VRT_new_backend_clustered`` for VSM
efficiency) in order to create the director. Varnish doesn't expose
its data structure for actual backends, only the director abstraction
and dynamic backends are built just like static backends, one *struct*
at a time. You can get rid of the ``struct vrt_backend`` as soon as
you have the ``struct director``.

A (dynamic) backend can't exceed its VCL's lifespan, because native
backends are *owned* by VCLs. Though a dynamic backend can't outlive
its VCL, it can be deleted any time with ``VRT_delete_backend``. The
VCL will delete the remaining backends once discarded, you don't need
to take care of it.

.. XXX this does not quite work yet because the deleted backend could
   be referenced, but at least that's where we want to get to. See
   also https://github.com/varnishcache/varnish-cache/pull/2725

Finally, Varnish will take care of event propagation for *all* native backends,
but dynamic backends can only be created when the VCL is warm. If your backends
are created by an independent thread (basically outside of VCL scope) you must
subscribe to VCL events and watch for VCL state (see
:ref:`ref-vmod-event-functions`). Varnish will panic if you try to create a
backend on a cold VCL, and ``VRT_new_backend`` will return ``NULL`` if the VCL
is cooling. You are also encouraged to comply with the
:ref:`ref_vcl_temperature` in general.


.. _ref-writing-a-director-loadbalancer:

Health Probes
=============

It is possible in a VCL program to query the health of a director (see
:ref:`std.healthy()`). A director can report its health if it implements the
``healthy`` function, it is otherwise always considered healthy.

Unless you are making a dynamic backend, you need to take care of the
health probes yourselves. For *load balancing* directors, being
healthy typically means having at least one healthy underlying backend
or director.

For dynamic backends, it is just a matter of assigning the ``probe`` field in
the ``struct vrt_backend``. Once the director is created, the probe definition
too is no longer needed. It is then Varnish that will take care of the health
probe and disable the feature on a cold VCL (see
:ref:`ref-vmod-event-functions`).

Instead of initializing your own probe definition, you can get a ``VCL_PROBE``
directly built from VCL (see :ref:`ref-vmod-vcl-c-types`).


Custom Backends
===============

If you want to implement a custom backend, have a look at how Varnish
implements native backends. It is the canonical implementation, and
though it provides other services like connection pooling or
statistics, it is essentially a director which state is a ``struct
backend``. Varnish native backends currently speak HTTP/1 over TCP or
UDS, and as such, you need to make your own custom backend if you want
Varnish to do otherwise such as connect over UDP or speak a different
protocol. A custom backend implementation must implement the ``gethdrs``
method, and optionally ``http1pipe``. It is the responsibility of the
custom backend to raise the ``send_failed`` flag from ``struct busyobj``.

If you want to leverage probes declarations in VCL, which have the advantage of
being reusable since they are only specifications, you can. However, you need
to implement the whole probing infrastructure from scratch.

You may also consider making your custom backend compliant with regards to the
VCL state (see :ref:`ref-vmod-event-functions`).


Data structure considerations
-----------------------------

When you are creating a custom backend, you may want to provide the semantics
of the native backends. In this case, instead of repeating the redundant fields
between data structures, you can use the macros ``VRT_BACKEND_FIELDS`` and
``VRT_BACKEND_PROBE_FIELDS`` to declare them all at once. This is the little
dance Varnish uses to copy data between the ``struct vrt_backend`` and its
internal data structure for example.

The copy can be automated with the macros ``VRT_BACKEND_HANDLE`` and
``VRT_BACKEND_PROBE_HANDLE``. You can look at how they can be used in the
Varnish code base.
