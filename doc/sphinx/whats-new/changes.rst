.. _whatsnew_changes:

Changes in Varnish 4.1
======================

Varnish 4.1 is the continuation of the new streaming architecture seen
in Varnish 4.0.


Proactive security features
===========================

New in 4.1 is support for different kinds of privilege separation methods,
collectively described as jails.

On most systems, the Varnish parent process will now drop effective
privileges to normal user mode when not doing operations needing special
access.

The Varnish worker child should now be run as a separate `vcache` user.

``varnishlog``, ``varnishncsa`` and other Varnish shared log utilities
now must be run in a context with `varnish` group membership.


Warm and cold VCL configurations
================================

Traditionally Varnish have had the concept of active and inactive
loaded VCLs.  Any loaded VCL lead to state being kept, and a separate
set of health checks (if configured) were being run against the backends.

To avoid the extra state and backend polling, a loaded VCL is now either
warm or cold. Runtime state (incl. backend counters) and health checks
are not present for cold VCLs.

A warm VCL will automatically be set to cold after `vcl_cooldown` seconds.

Output from `vcl.list`::

    varnish> vcl.list
    200
    available  auto/warm       0 boot
    available  auto/warm       0 62f5275f-a937-4df9-9fbb-c12336bdfdb8


A single VCL's state can be changed with the `vcl.state` call in
``varnishadm``::

    vcl.state <configname> <state>
        Force the state of the specified configuration.
        State is any of auto, warm or cold values.

Example::


    varnish> vcl.state 62f5275f-a937-4df9-9fbb-c12336bdfdb8 cold
    200

    varnish> vcl.list
    200
    available  auto/warm       0 boot
    available  auto/cold       0 62f5275f-a937-4df9-9fbb-c12336bdfdb8


VMOD writers should read up on the new vcl_event system to
release unnecessary state when a VCL is transitioned to cold (see
:ref:`ref-vmod-event-functions`).


PROXY protocol support
======================

Socket support for PROXY protocol connections has been added. PROXY
defines a short preamble on the TCP connection where (usually) a SSL/TLS
terminating proxy can signal the real client address.

The ``-a`` startup argument syntax has been expanded to allow for this::

    $ varnishd -f /etc/varnish/default.vcl -a :6081 -a 127.0.0.1:6086,PROXY

Both PROXY1 and PROXY2 protocols are supported on the resulting listening
socket.

For connections coming in over a PROXY socket, ``client.ip`` and
``server.ip`` will contain the addresses given to Varnish in the PROXY
header/preamble (the "real" IPs).

The new VCL variables ``remote.ip`` and ``local.ip`` contains the local
TCP connection endpoints. On non-PROXY connections these will be identical
to ``client.ip`` and ``server.ip``.

An expected pattern following this is `if (std.port(local.ip) == 80) { }`
in ``vcl_recv`` to see if traffic came in over the HTTP listening socket
(so a client redirect to HTTPS can be served).


VMOD backends
=============

Before Varnish 4.1, backends could only be declared in native VCL. Varnish
4.0 moved directors from VCL to VMODs, and VMODs can now also create
backends. It is possible to both create the same backends than VCL but
dynamically, or create backends that don't necessarily speak HTTP/1 over
TCP to fetch resources. More details in the :ref:`ref-writing-a-director`
documentation.


Backend connection timeout
==========================

Backend connections will now be closed by Varnish after `backend_idle_timeout`
seconds of inactivity.

Previously they were kept around forever and the backend servers would close
the connection without Varnish noticing it. On the next traffic spike needing
these extra backend connections, the request would fail, perhaps multiple
times, before a working backend connection was found/created.


Protocol support
================

Support for HTTP/0.9 on the client side has been retired.


More modules available
======================

Varnish has an ecosystem for third-party modules (vmods). New since
the last release, these are worth knowing about:

libvmod-saintmode: Saint mode ("inferred health probes from traffic") was taken
out of Varnish core in 4.0, and is now back as a separate vmod. This is useful
for detecting failing backends before the health probes pick it up.

libvmod-xkey: Secondary hash keys for cache objects, based on the hashtwo vmod
written by Varnish Software. Allows for arbitrary grouping of objects to be
purged in one go, avoiding use of ban invalidation. Also known as Cache Keys or
Surrogate Key support.

libvmod-rtstatus: Real time statistics dashboard.


Passing data between ESI requests
=================================

A new `req_top` identifier is available in VCL, which is a reference to
`req` in the top-level ESI request.

This is useful to pass data back and forth between the main ESI request
and any ESI sub-requests it leads to.


Other noteworthy small changes
==============================

* Varnish will now use the ``stale-while-revalidate`` defined in RFC5861
  to set object grace time.
* -smalloc storage is now recommended over -sfile on Linux systems.
* New VCL variable ``beresp.was_304`` has been introduced in
  ``vcl_backend_response``. Will be set to ``true`` if the response
  from the backend was a positive result of a conditional fetch (``304
  Not Modified``).

