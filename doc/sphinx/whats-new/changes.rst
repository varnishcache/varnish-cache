.. _whatsnew_changes:

Changes in Varnish 4.1 (unreleased)
===================================

Varnish 4.1 is the continuation of the new streaming architecture seen in Varnish 4.0.


Proactive security features
===========================

New in 4.1 is support for different kinds of privilege separation methods,
collectively described as jails.

On most systems, the Varnish parent process will now drop effective privileges
to normal user mode when not doing operations needing special access.

The Varnish worker child should now be run as a separate `vcache` user.

``varnishlog``, ``varnishncsa`` and other Varnish shared log utilities now must
be run in a context with `varnish` group membership.


Warm and cold VCL configurations
================================

Traditionally Varnish have had the concept of active and inactive loaded VCLs.
Any loaded VCL lead to state being kept, and a separate set of health checks (if
configured) were being run against the backends.

To avoid the extra state and backend polling, a loaded VCL is now either warm
or cold. Runtime state (incl. backend counters) and health checks are not
present for cold VCLs.

A warm VCL will automatically be set to cold after `vcl_cooldown` seconds.

Output from `vcl.list`::

    varnish> vcl.list
    200
    available  auto/warm       0 boot
    available  auto/warm       0 62f5275f-a937-4df9-9fbb-c12336bdfdb8


A single VCL's state can be chanced with the `vcl.state` call in
``varnishadm``::

    vcl.state <configname> [auto|cold|warm]
        Force the state of the named configuration.

Example::


    varnish> vcl.state 62f5275f-a937-4df9-9fbb-c12336bdfdb8 cold
    200

    varnish> vcl.list
    200
    available  auto/warm       0 boot
    available  auto/cold       0 62f5275f-a937-4df9-9fbb-c12336bdfdb8


VMOD writers should read up on the new vcl_event system to release unnecessary
state when a VCL is transitioned to cold (see ref:`ref-vmod-event-functions`).


PROXY protocol support
======================

Socket support for PROXY protocol connections has been added. PROXY defines a
short preamble on the TCP connection where (usually) a SSL/TLS terminating
proxy can signal the real client address.

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

Not yet documented.


Surrogate keys
==============

Not yet documented.

Passing data between ESI requests
=================================

A new `req_top` identifier is available in VCL, which is a reference
to `req` in the top-level ESI request.

This is useful to pass data back and forth between the main ESI request
and any ESI subrequests it lead to.


Other noteworthy small changes
==============================

* Varnish will now use the ``stale-while-revalidate`` defined in RFC5861 to set object grace time.
* Varnish will now discard remaining/older open backend connections when a failing connection is found.
* -smalloc storage is now recommended over -sfile on Linux systems.

