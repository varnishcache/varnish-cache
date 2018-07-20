.. _whatsnew_upgrading_6.0:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 6.0
%%%%%%%%%%%%%%%%%%%%%%%%

.. _upd_6_0_uds_acceptor:

Unix domain sockets as listen addresses
=======================================

The ``varnishd -a`` command-line argument now has this form, where the
``address`` may be a Unix domain socket, identified as such when it
begins with ``/`` (see varnishd :ref:`ref-varnishd-options`)::

  -a [name=][address][:port][,PROTO][,user=<user>][,group=<group>][,mode=<mode>]

For example::

  varnishd -a /path/to/listen.sock,PROXY,user=vcache,group=varnish,mode=660

That means that an absolute path must always be specified for the
socket file.  The socket file is created when Varnish starts, and any
file that may exist at that path is unlinked first. You can use the
optional ``user``, ``group`` and ``mode`` sub-arguments to set
permissions of the new socket file; use names for ``user`` and
``group`` (not numeric IDs), and a 3-digit octal number for
``mode``. This is done by the management process, so creating the
socket file and setting permissions are done with the privileges of
the management process owner.

There are some platform-specific restrictions on the use of UDSen to
which you will have to conform. Here are some things we know of, but
this list is by no means authoritative or exhaustive; always consult
your platform documentation (usually in ``man unix``):

* There is a maximum permitted length of the path for a socket file,
  considerably shorter than the maximum for the file system; usually a
  bit over 100 bytes.

* On FreeBSD and other BSD-derived systems, the permissions of the
  socket file do not restrict which processes can connect to the
  socket.

* On Linux, a process connecting to the socket must have write
  permissions on the socket file.

On any system, a process connecting to the socket must be able to
access the socket file. So you can reliably restrict access by
restricting permissions on the directory containing the socket (but
that must be done outside of the Varnish configuration).

When UDS listeners are in use, VCL >= 4.1 will be required for all VCL
programs loaded by Varnish. If you attempt to load a VCL source with
``vcl 4.0;``, the load will fail with a message that the version is
not supported.

If you continue using only IP addresses in your ``-a`` arguments, you
won't have to change them, and you can continue using VCL 4.0.

.. _upd_6_0_uds_backend:

Unix domain sockets as backend addresses
========================================

A backend declaration may now have the ``.path`` field to specify a
Unix domain socket to which Varnish connects::

  backend my_uds_backend {
	.path = "/path/to/backend.sock";
  }

One of the fields ``.host`` or ``.path`` must be specified for a
backend (but not both).

The value of ``.path`` must be an absolute path (beginning with
``/``), and the file at that path must exist and be accessible to
Varnish at VCL load time; and it must be a socket.

The platform-specific restrictions on UDSen mentioned above apply of
course to backends as well; but in this case your deployment of the
peer component listening at the socket file must fulfill those
conditions, otherwise Varnish may not be able to connect to the
backend.

The path of a socket file may also be specified in the
``varnishd -b`` command-line option (see varnishd
:ref:`ref-varnishd-options`)::

  $ varnishd -b /path/to/backend.sock

The value of ``-b`` must fulfill the same conditions as the ``.path``
field in a backend declaration.

Backends with the ``.path`` specification require VCL 4.1, as do paths
with the ``-b`` argument. If you don't use UDS backends, you can
continue using VCL 4.0.

varnishd parameters
===================

The ``cli_buffer`` parameter, which was deprecated as of Varnish 5.2,
is now retired.

:ref:`ref_param_max_restarts` now works more correctly -- it is the
number of ``return(restart)`` calls permitted per request. (It had
been one less than the number of permitted restarts.)

The parameters :ref:`ref_param_tcp_keepalive_intvl`,
:ref:`ref_param_tcp_keepalive_probes` and
:ref:`ref_param_tcp_keepalive_time` are silently ignored for listen
addresses that are Unix domain sockets. The parameters
:ref:`ref_param_accept_filter` and :ref:`ref_param_tcp_fastopen`
(which your platform may or may not support in the first place) almost
certainly have no effect on a UDS. It is not an error to use any of
these parameters with a UDS; you may get error messages in the log for
``accept_filter`` or ``tcp_fastopen`` (with the VSL tag ``Error`` in
raw grouping), but they are harmless.

:ref:`ref_param_workspace_thread` is now used for IO buffers during
the delivery of the client response. This space had previously been
taken from :ref:`ref_param_workspace_client`. If you need to reduce
memory footprint, consider reducing ``workspace_client`` by the amount
in ``workspace_thread``.

Added :ref:`ref_param_esi_iovs`. tl;dr: Don't touch it, unless advised
to do so by someone familiar with the innards of Varnish.

Changes to VCL
==============

VCL 4.0 and 4.1
~~~~~~~~~~~~~~~

The first line of code in a VCL program may now be either ``vcl 4.0;``
or ``vcl 4.1;``, establishing the version of the language for that
instance of VCL. Varnish 6.0 supports both versions.

The VCL version mainly affects which variables may be used in your VCL
program, or in some cases, whether the variable is writable or
read-only. Only VCL 4.1 is permitted when Unix domain sockets are in
use.

For details, see :ref:`vcl_variables`, and the notes in the present
document.

VCL variables
~~~~~~~~~~~~~

``local.socket`` and ``local.endpoint``
---------------------------------------

These read-only variables are available as of VCL 4.1, and provide
information about the listener address over which the current client
request was received.

``local.socket`` is the name provided in the ``-a`` command-line
argument for the current listener, which defaults to ``a0``, ``a1``
and so on (see varnishd :ref:`ref-varnishd-options`).

``local.endpoint`` is the value of the ``address[:port]`` or ``path``
field provided as the ``-a`` value for the current listener, exactly
as given on the command line. For example::

  # When varnishd is invoked with these -a arguments ...
  $ varnishd -a foo=12.34.56.78:4711 -a bar=/path/to/listen.sock

  # ... then in VCL, for requests received over the first listener:
  local.socket == "foo"
  local.endpoint == "12.34.56.78:4711"

  # ... and for requests received over the second listener:
  local.socket == "bar"
  local.endpoint == "/path/to/listen.sock"

  # With this invocation ...
  $ varnishd -a :80 -a 87.65.43.21

  # ... then for requests received over the first listener:
  local.socket == "a0"
  local.endpoint == ":80"

  # ... and for the second listener
  local.socket == "a1"
  local.endpoint == "87.65.43.21"

So if you have more than one listener and need to tell them apart in
VCL, for example a listener for "regular" client traffic and another
one for "admin" requests that you must restrict to internal systems,
these two variables can help you do so.

``local.socket`` and ``local.endpoint`` are available on both the
client and backend sides. But the values on the backend side are not
necessarily the same as they were on the side of the client request
that initiated the backend request. This is because of the separation
of client and backend threads -- a backend thread may be re-used that
was initiated by a client request over another listener, and
``local.socket`` and ``local.endpoint`` on that thread retain the
values for the original listener.

So if, in your backend VCL code, you need to be sure about the
listener that was used on the client side of the same transaction,
assign ``local.socket`` and/or ``local.endpoint`` to a client request
header, and retrieve the value from a backend request header::

  sub vcl_miss {
	set req.http.X-Listener = local.socket;
  }

  sub vcl_backend_fetch {
	if (bereq.http.X-Listener == "a0") {
		# ...
	}
  }

``sess.xid``
------------

This is the unique ID assigned by Varnish to the current session,
which stands for the "conversation" with a single client connection
that comprises one or more request/response transactions. It is the
same XID shown in the log for session transactions (with
``-g session`` grouping). ``sess.xid`` is read-only and is available
as of VCL 4.1.

Variable changes in VCL 4.0 and 4.1
-----------------------------------

The ``*.proto`` variables (``req.proto``, ``resp.proto``,
``bereq.proto`` and ``beresp.proto``) are read-only as of VCL 4.1, but
are still writable in VCL 4.0.

``req.esi`` is available in VCL 4.0, but no longer in 4.1. In its
place, ``resp.do_esi`` has been introduced in VCL 4.1. Set
``resp.do_esi`` to false in ``vcl_deliver`` if you want to selectively
disable ESI processing for a client response (even though
``beresp.do_esi`` was true during fetch).

``beresp.backend.ip`` and ``beresp.storage_hint`` are discontinued as
of VCL 4.1, but are still available in 4.0. Note that
``beresp.storage_hint`` has been deprecated since Varnish 5.1; you
should use ``beresp.storage`` instead.

Client-side variable access
---------------------------

``req.storage``, ``req.hash_ignore_busy`` and ``req.hash_always_miss``
are now accessible from all of the client side subroutines (previously
only in ``vcl_recv{}``).

Unix domain sockets and VCL
~~~~~~~~~~~~~~~~~~~~~~~~~~~

We have made an effort to adapt the support of Unix domain sockets in
VCL so that you may not have to change anything in your VCL deployment
at all, other than changing the version to 4.1.

The short story is that where VCL requires an IP value, the value is
``0.0.0.0:0`` for a connection that was addressed as a UDS -- the "any
IPv4" address with port 0. So your use of IP-valued elements in VCL
will continue to work and may not have to change, but there are some
consequences that you should consider, covered in the following.

As we shall see, for a variety of reasons you get the best results if
the component forwarding to Varnish via UDS uses the PROXY protocol,
which sets ``client.ip`` and ``server.ip`` to the addresses sent in
the PROXY header.

If you don't use UDSen, then nothing about VCL changes with respect to
network addressing. UDS support requires version 4.1, so if you are
keeping your VCL level at 4.0 (and hence are staying with IP
addresses), then none of the following is of concern.

``client.ip``, ``server.ip``, ``local.ip`` and ``remote.ip``
------------------------------------------------------------

These variables have the value ``0.0.0.0`` for a connection that was
addressed as a UDS. If you are using the PROXY protocol, then
``client.ip`` and ``server.ip`` have the "real" IP address values sent
via PROXY, but ``local.ip`` and ``remote.ip`` are always ``0.0.0.0``
for a UDS listener.

If you have more than one UDS listener (more than one ``-a``
command-line argument specifying a socket path), then you may not be
able to use the ``*.ip`` variables to tell them apart, especially
since ``local.ip`` will be ``0.0.0.0`` for all of them. If you need to
distinguish such addresses in VCL, you can use ``local.socket``, which
is the name given for the ``-a`` argument (``a0``, ``a1`` etc. by
default), or ``local.endpoint``, which in the case of UDS is the path
given in the ``-a`` argument. You can, for example, use string
operations such as regex matching on ``local.endpoint`` to determine
properties of the path address::

  # admin requests allowed only on the listener whose path ends in
  # "admin.sock"
  if (req.url ~ "^/admin") {
  	if (local.endpoint !~ "admin.sock$") {
		# wrong listener, respond with "403 Forbidden"
		return( synth(403) );
	}
	else {
		# process the admin request ...
	}
  }

  # superadmin requests only allowed on the "superadmin.sock" listener
  if (req.url ~ "^/superadmin") {
  	if (local.endpoint !~ "superadmin.sock$") {
		return( synth(403) );
	}
	else {
		# superadmin request ...
	}
  }

ACLs
----

As before, ACLs can only specify ranges of IP addresses, and matches
against ACLs can only be run against IP-valued elements.

This means that if a ``*.ip`` variable whose value is ``0.0.0.0`` due
to the use of UDS is matched against an ACL, the match can only
succeed if the ACL includes ``0.0.0.0``. If you currently have a
security requirement that depends on IP addresses *not* matching an
ACL unless they belong to a specified range, then that will continue
to work with a UDS listener (since you almost certainly have not
included ``0.0.0.0`` in that range).

Recall again that ``client.ip`` and ``server.ip`` are set by the PROXY
protocol. So if you have a UDS listener configured to use PROXY and
are using an ACL to match against one of those two variables, the
matches will continue working against the "real" IPs sent via PROXY.

You can of course define an ACL to match in the UDS case, by including
``0.0.0.0``::

  # matches local.ip and remote.ip when the listener is UDS
  acl uds {
  	"0.0.0.0";
  }

But such an ACL cannot distinguish different UDS listeners, if you
have more than one. For that, you can achieve a similar effect by
inspecting ``local.socket`` and/or ``local.endpoint``, as discussed
above.

``client.identity`` and the hash and shard directors
----------------------------------------------------

As before, ``client.identity`` defaults to ``client.ip``; that is, if
its value has not been explicitly set in VCL, then it returns the same
value as ``client.ip`` when it is read.

A common use of ``client.identity`` is to configure the hash and shard
directors (see :ref:`vmod_directors(3)`). This is a way to achieve
"client-sticky" distribution of requests to backends -- requests from
the same clients are always sent to the same backends.

Such a configuration will almost certainly not do what you want if:

* The listener is set to a UDS address.
* PROXY is not used to set ``client.ip``.
* ``client.identity`` is not set to a distinct value before it is
  used to configure the director.

Since ``client.identity`` defaults to ``client.ip``, which is always
``0.0.0.0`` under these conditions, the result will be that the
director sends all requests to just one backend, and no requests to
any other backend.

To avoid that result, change one of the conditions listed above -- use
PROXY to set distinct values for ``client.ip``, or set
``client.identity`` to distinct values before it is used.

``server.ip`` and default hashing for the cache
-----------------------------------------------

The default algorithm for computing a hash value for the cache (the
implementation of ``vcl_hash`` in ``builtin.vcl``) mixes ``req.url``
and the Host header (``req.http.Host``) into the hash data. If there
is no Host header, then ``server.ip`` is used instead. Considering the
Host header or ``server.ip`` is a way of achieving a kind of "virtual
hosting" -- if your site receives requests with different Host headers
or at distinct server addresses, then requests for the same URL will
not hit the same cached response, if the requests are different in
those other respects.

If you have UDS listeners and are not using PROXY to set distinct
values of ``server.ip``, then requests without a Host header will have
the same value of ``server.ip == 0.0.0.0`` mixed into the hash. In
that case, requests with the same URL will result in the same hash
value, and hit the same cached responses.

That doesn't matter, of course, if you don't need the "virtual
hosting" effect -- you only have one listener, you never receive
different host headers, or you never receive the same URL for what
should lead to distinct responses.

But if you need to avoid that result, then you can make one or more
of these changes:

* Use the PROXY protocol to set distinct ``server.ip`` values.
* Write your own implementation of ``vcl_hash``, for example to
  mix ``local.socket`` or ``local.endpoint`` into the hash.
* Set ``req.http.Host`` to a distinct value if it is absent before
  ``vcl_hash`` is entered.

X-Forwarded-For
---------------

Varnish automatically appends the value of ``client.ip`` to the
``X-Forwarded-For`` request header that is passed on to backends, or
it creates the header with that value if it is not already present in
the client request.

If the client request is received over a UDS listener and the PROXY
protocol is not used, then ``0.0.0.0`` will be added to
``X-Forwarded-For``.  If you prefer, you can change that in VCL::

  sub vcl_backend_fetch {
  	# Assuming that server.identity has been set to an IP
	# address with the -i command-line argument.
	set bereq.http.X-Forwarded-For
	    = regsub(bereq.http-X-Forwarded-For, "0.0.0.0$", server.identity);
	# ...
  }

Again, this is probably not a concern if ``client.ip`` is set via the
PROXY protocol.

UDS backends and the Host header
--------------------------------

By default, Varnish forwards the Host header from a client request to
the backend. If there is no Host header in the client request, and the
``.host_header`` field was set in the backend declaration, then that
value is used for the backend Host header. For backends declared with
the ``.host`` field (with a domain name or IP address), then if there
is neither a client Host header nor a ``.host_header`` declaration,
the value of ``.host`` is set as the Host header of the backend
request.

If the backend was declared with ``.path`` for a socket path, then the
backend Host header is set to ``0.0.0.0`` under those conditions.

To re-state that:

* If the backend was declared with ``.path`` to connect to a Unix
  domain socket, ...

* and ``.host_header`` was not set in the backend declaration, ...

* and there is no Host header in the client request, ...

* then the Host header in the backend request is set to ``0.0.0.0``.

If you want to avoid that, set a ``.host_header`` value for the
backend, or set a value for the Host header in VCL.

VMOD std
--------

:ref:`std.port(IP) <func_port>` always returns 0 when applied to a
``*.ip`` variable whose value is set to ``0.0.0.0`` because the
listener is UDS.  :ref:`std.set_ip_tos(INT) <func_set_ip_tos>` is
silently ignored when the listener is UDS.

The ``shard`` director
----------------------

The ``alg`` argument of the shard director's ``.reconfigure()`` and
``.key()`` methods has been removed. The choice of hash algorithms was
experimental, and we have settled on SHA256 as providing the best
dispersal.

If you have been using other choices of ``alg`` for
``.reconfigure()``, then after upgrading and removing ``alg``, the
sharding of requests to backends will change once and only once.

If you have been using other values of ``alg`` for ``.key()`` and need
to preserve the previous behavior, see the
`change log <https://github.com/varnishcache/varnish-cache/blob/master/doc/changes.rst>`_
for advice on how to do so.

With the ``resolve=LAZY`` argument of the ``.backend()`` method, the
shard director will now defer the selection of a backend to when a
backend connection is actually made, which is how all other bundled
directors work as well.

In ``vcl_init``, ``resolve=LAZY`` is default and enables layering the
shard director below other directors -- you can now use something like
``mydirector.add_backend(myshard.backend())`` to set the shard
director as a backend for another director.

Use of ``resolve=LAZY`` on the client side is limited to using the
default or associated parameters.

The shard director now provides a ``shard_param`` object that serves
as a store for a set of parameters for the director's ``.backend()``
method. This makes it possible to re-use a set of parameter values
without having to restate them in every ``.backend()`` call. The
``.backend()`` method has an argument ``param`` whose value, if it is
used, must be returned from the ``shard_param.use()`` method.

Because of these changes, support for positional arguments of the
shard director ``.backend()`` method had to be removed. In other
words, all parameters to the shard director ``.backend()`` method now
need to be named.

See :ref:`vmod_directors(3)` for details.

Restarts
~~~~~~~~

Restarts now leave all of the properties of the client request
unchanged (all of the ``req.*`` variables, including the headers),
except for ``req.restarts`` and ``req.xid``, which change by design.

If you need to reset the client request headers to their original
state (before changes in VCL), call
:ref:`std.rollback(req) <func_rollback>`.

``return(restart)`` can now be called from ``vcl_recv{}``.

New VMODs
~~~~~~~~~

VMOD unix
---------

:ref:`vmod_unix(3)` provides functions to determine the credentials of
the peer process (user and group of the process owner) that connected
to Varnish over a listener at a Unix domain socket. You can use this,
for example, to impose tighter restrictions on who can access certain
resources::

  import unix;

  sub vcl_recv {
	# Return "403 Forbidden" if the connected peer is
	# not running as the user "trusteduser".
	if (unix.user() != "trusteduser") {
		return( synth(403) );
	}

This is not available on every platform. As always, check the
documentation and test the code before you attempt something like this
in production.

VMOD proxy
----------

:ref:`vmod_proxy(3)` provides functions to extract TLV attributes that
may be optionally sent over a PROXYv2 connection to a Varnish listener.
Most of these are properties of the peer component's TLS connection::

  import proxy;

  # Get the authority attribute -- corresponds to the SNI of a TLS
  # connection.
  set req.http.Authority = proxy.authority();

Not all implementations send TLV attributes, and those that do don't
necessarily support all of them; test your code to see what works in
your configuration.

See the
`PROXY v2 specification <https://www.haproxy.org/download/1.8/doc/proxy-protocol.txt>`_ for more information about TLV attributes.

Packaging changes
=================

Supported platforms
~~~~~~~~~~~~~~~~~~~

Official Varnish packages went through major changes for this release,
and target Debian 9, Ubuntu 16.04 LTS and (Red Hat) Enterprise
Linux 7. Ubuntu 14.04 LTS will likely reach its end of life before
Varnish 6 and the venerable Enterprise Linux 6 is getting too old and
forced time-consuming workarounds so for these reasons we dropped
community support for those platforms.

Services
~~~~~~~~

As a result we ended up with systemd-only platforms for
the official packages. The old services are still available as we
archived them in the ``pkg-varnish-cache`` source tree. This was the
occasion to remove differences between Red Hat and Debian derivatives
since there's no more reasons to have them diverge: we initially
inherited packaging support from downstream package maintainers, and
they deserve many thanks for that.

Another big difference between Red Hat and Debian derivatives was the
way we handled VCL reloads via the service manager. We introduced a
new ``varnishreload`` script that operates on top of ``varnishadm``
to perform hot reloads of one VCL configuration or label at a time.
All you need is enough privileges to contact ``varnishd``'s command
line interface, which should not be a problem for package managers.

Once the ``varnish`` package is installed, you can learn more::

  varnishreload -h

Again, many thanks to downstream maintainers and some early adopters
for their help in testing the new script.

To stay on the topic of the command line interface, packages no longer
create a secret file for the CLI, and services omit ``-S`` and ``-T``
options on the ``varnishd`` command. This means that out of the box,
you can only connect to the CLI locally with enough privileges to read
a secret generated randomly. This means less noise in our packages,
and you need to change the service configuration to enable remote
access to the CLI. With previous packages, you also needed to change
your configuration because the CLI would only listen to the loopback
interface anyway.

To change how ``varnishd`` is started, please refer to the systemd
documentation.

Virtual provides
~~~~~~~~~~~~~~~~

Last but not least in the packaging space, we took a first step towards
improving dependency management between official ``varnish`` packages
and VMODs built on top of them. RPMs and Deb packages now provide the
strict and VRT ABIs from ``varnishd`` and the goal is to ultimately
prevent a package installation or upgrade that would prevent a VMOD
from being loaded.

For Deb packages::

  Provides:
   varnishd-abi-SHA1,
   varnishd-vrt (= x.y)

And for RPMs::

  Provides: varnishd(abi)(x86-64) = SHA1
  Provides: varnishd(vrt)(x86-64) = x.y

For VMOD authors or downstream distributors, this means that depending
on the ``$ABI`` stanza in the VMOD descriptor, they can either tie their
backend manually to the git hash Varnish was built from or to the VRT
version used at the time.

For example, a VMOD RPM built against Varnish 6.0.0 could have::

  Requires: varnishd(vrt)%{?_isa} >= 7.0
  Requires: varnishd(vrt)%{?_isa} < 8

Future plans include the ability to automate this for out-of-tree VMODs
and remove manual steps. To learn more about the history behind this
change, it was formalized via the Varnish Improvement Process:

https://github.com/varnishcache/varnish-cache/wiki/VIP20%3A-Varnish-ABI-and-packaging

Another thing available only to RPM packages as of 6.0.0 is virtual
provides for VMODs.

Instead of showing shared objects that aren't even in the dynamic
linker's default path::

  Provides: libvmod_std.so(64bit)
  Provides: libvmod_directors.so(64bit)
  [...]

You get virtual VMOD provides with a version::

  Provides: vmod(std)(x86-64) = 6.0.0-1
  Provides: vmod(directors)(x86-64) = 6.0.0-1
  [...]

With the same mechanism it becomes possible to require a VMOD directly
and let it bring along its dependencies, like ``varnish``. As this is
currently not automated for out-of-tree VMODs, consider this a preview
of what you will be able to do once VIP 20 is completed.

Other changes
=============

* ``varnishd(1)``:

  * The ``umem`` storage allocator, which was removed as of Varnish
    5.1, has been restored and is now the default on a system where
    ``libumem`` is available (SunOS and descendants).

* ``varnishlog(1)``:

  * Added a third field to the ``ReqStart`` log record that contains the
    name of the listener address over which the request was received, see
    :ref:`vsl(7)`.

  * ``0.0.0.0`` and port ``0`` appear in the logs where an IP and port
    otherwise appear, when the connection in question was addressed as
    a Unix domain socket. This affects ``ReqStart``, ``SessOpen``,
    ``BackendStart`` and ``BackendOpen``.

    If you have more than one UDS listener, they can be distinguished
    with the "listener name" field -- the third field for both
    ``ReqStart`` and ``SessOpen``.

    If you have more than one UDS backend, they can be distinguished
    with the backend name field -- the second field in
    ``BackendOpen``.

  * The byte counters logged with ``ReqAcct`` now report the numbers
    returned from the operating system telling us how many bytes were
    actually sent in a request and response, rather than what Varnish
    thought it was going to send. This gives a more accurate account
    when there are errors, for example when a client hung up early
    without receiving the entire response. The figures also include
    any overhead in a request or response body, for example due to
    chunked encoding.

  * Debugging logs for the PROXY protocol are turned off by default.
    They can be turned on with the ``protocol`` flag of the varnishd
    :ref:`ref_param_debug` parameter (``-p debug=+protocol``).

* ``varnishstat(1)``

  * Added the counter ``cache_hit_grace`` -- how often objects in the
    cache were hit when their TTL had expired, but they were still
    in grace.

* ``varnishncsa(1)``

  * The ``%h`` formatter (remote host) gets its value from
    ``ReqStart`` for client requests and ``BackendStart`` for backend
    requests.  The value will be ``0.0.0.0`` for client requests when
    the listener is UDS, and for backend requests when the backend is
    UDS.

  * The ``%r`` formatter (first line of the request) is reconstructed
    in part from the Host request header. For UDS backends, Host may
    be ``0.0.0.0`` for the reasons explained above (no client Host
    header and no ``.host_header`` setting for the backend), so that
    may appear in the output for ``%r``. You can avoid that with the
    measures discussed above.

  * If you have more than one UDS listener and/or more than one UDS
    backend, and you want to tell them apart in the ``varnishncsa``
    output (rather than just see ``0.0.0.0``), use the ``%{VSL}x``
    formatter to capture the listener name and the backend name.

    For the listener name, use ``%{VSL:ReqStart[3]}x`` for client logs
    (the third field of ``ReqStart`` logs).

    For the backend name, use ``%{VSL:BackendOpen[2]}x`` for backend
    logs.

  * varnishncsa does not accept output format strings (from the ``-F``
    command-line argument or a configuration file) if they specify
    tags for log entries whose payloads may contain control or binary
    characters.

* ``varnishtest(1)`` and ``vtc(7)``:

  * The ``client -connect`` and ``server -listen`` commands in vtc
    scripts now allow Unix domain sockets as addresses, recognized
    when the argument begins with a ``/``.

    A client attempts the connection immediately, so the socket file
    must exist at the given path when the client is started, and the
    client must be able to access it.

    The ``server -listen`` command must be able to create the socket
    file when it executes ``bind(2)``. To make it easier for other
    processes to connect to the socket, the server's umask is
    temporarily set to 0 before the listen is attempted, to minimize
    issues with permissions. No further attempt is made to set the
    socket's permissions.

    To test a Varnish instance listening at a UDS, just use the
    ``varnish -arg`` command with the appropriate settings for the
    ``-a`` command line argument, see :ref:`varnishd(1)`.

    The ``varnish -vcl+backend`` command now works to include backend
    definitions for server objects that are listening at UDS. Backend
    declarations are implicitly included for such servers with the
    appropriate ``.path`` setting.

    A convenient location for socket files to be used in a test is the
    temporary directory created by ``varnishtest`` for each test,
    whose path is held in the macro ``${tmpdir}``. So this is a common
    idiom for tests that involve UDSen::

      server s1 -listen "${tmpdir}/s1.sock" { ... } -start

      varnish v1 -arg "-a ${tmpdir}/v1.sock" -vcl+backend { ... } -start

      client c1 -connect "${tmpdir}/v1.sock" { ... } -run

    When a Varnish instance in a vtc test is listening at a UDS, then
    its ``vN_*`` macros are set like this:

    * ``v1_addr``: ``/path/to/socket``
    * ``v1_port``: ``-`` (hyphen)
    * ``v1_sock``: ``/path/to/socket -``

    When a server ``s1`` is listening at a UDS:

    * ``s1_addr``: ``0.0.0.0``
    * ``s1_port``: ``0``
    * ``s1_sock``: ``/path/to/socket``

    The vtc variables ``remote.ip`` and ``remote.port``, which can be
    used in ``expect`` expressions for both server and client scripts,
    are set to ``0.0.0.0`` and ``0``, respectively, when the peer
    address is a UDS.

    We have added the variable ``remote.path`` as a counterpart to the
    other two. Its value is the path when the peer address is a UDS,
    and NULL otherwise (matching ``<undef>`` in the latter case).

* Changes for developers:

  * The VRT API version has been bumped to 7.0, and comprises a variety
    of new additions and changes. See ``vrt.h`` and the
    `change log <https://github.com/varnishcache/varnish-cache/blob/master/doc/changes.rst>`_
    for details.

  * There are new rules about including API headers -- some may only
    be included once, others must included in a specific order. Only
    ``cache.h`` *or* ``vrt.h`` may be included (``cache.h`` includes
    ``vrt.h``). See the ``#error`` directives in the headers.

  * VMOD authors can use the ``VRT_VSC_*()`` series of functions and
    the new ``vsctool`` to create statistics for a VMOD that will be
    displayed by varnishstat.  Varnish uses the same technique to
    create its counters, so you can look to the core code to see how
    it's done.

  * The ``VCL_INT`` and ``VCL_BYTES`` types are now defined to be
    strictly 64 bit (rather than leave it to whatever your platform
    defines as ``long``). But you may not get that full precision,
    for reasons discussed in the
    `change log <https://github.com/varnishcache/varnish-cache/blob/master/doc/changes.rst>`_.

  * As part of VRT version 7.0, the ``path`` field has been added to
    to ``struct vrt_backend``, which a VMOD can use with
    ``VRT_new_backend()`` to create a dynamic backend with a UDS
    address (see ``vrt.h``).

    If ``path`` is non-NULL, then both of the IPv4 and IPv6 addresses
    must be NULL. If ``path`` is NULL, then (as before) one or both of
    the IP addresses must be non-NULL. The ``dyn_uds`` object in VMOD
    debug (available in the source tree) illustrates how this can be
    done.

  * VMOD vcc sources may now include a directive ``$Prefix``, whose
    value is the string prepended to the names of C objects and
    functions in the generated C interface (in ``vcc_if.h``). So you
    may choose another prefix besides ``vmod_``, if so desired.

  * vcc sources may also include a directive ``$Synopsis`` whose value
    may be ``auto`` or ``manual``, default ``auto``.

    When ``$Synopsis`` is ``auto``, the vmodtool generates a more
    comprehensive ``SYNOPSIS`` section in the documentation than in
    previous versions -- an overview of the objects, methods and
    functions in your VMOD, with their type signatures.

    When ``$Synopsis`` is ``manual``, the ``SYNOPSIS`` is left out of
    the generated docs altogether; so you can write the ``SYNOPSIS``
    section yourself, if you prefer.

  * Support for a new declaration of optional arguments in vcc files
    has been added: ``[ argname ]`` can be used to mark *argname* as
    optional.

    If this declaration is used for any argument, _all_ user arguments
    and ``PRIV_*`` pointers (no object pointers) to the respective
    function/method will be passed in a ``struct`` *funcname*\
    ``_arg`` specific to this function which contains the arguments by
    their name (or the name ``arg``\ *n* for unnamed arguments, *n*
    being the argument position starting with 1) plus ``valid_``\
    *argname* members for optional arguments which are being set to
    non-zero iff the respective *argname* was provided.

    Argument presence is determined at VCC time, so it is not possible
    to pass an unset argument from another function call.



*eof*
