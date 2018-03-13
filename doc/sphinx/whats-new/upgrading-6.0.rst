.. _whatsnew_upgrading_6.0:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 6.0
%%%%%%%%%%%%%%%%%%%%%%%%

XXX: Most important change first
================================

XXX ...

Unix domain sockets as listen addresses
=======================================

The ``varnishd -a`` command-line argument now has this form, where the
``address`` may be a Unix domain socket, identified as such when it
begins with ``/`` (see varnishd :ref:`ref-varnishd-options`)::

  -a [name=][address][:port][,PROTO][,user=<user>][,group=<group>][,mode=<mode>]

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

If you continue using only IP addresses in your ``-a`` arguments, you
won't have to change them.

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

varnishd parameters
===================

XXX: ...

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

Changes to VCL
==============

XXX: ... intro paragraph

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

If you don't use UDSen, then nothing about VCL changes. UDS support
requires version 4.1, so if you are keeping your VCL level at 4.0 (and
hence are staying with IP addresses), then none of the following is of
concern.

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

Other changes
=============

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

  * XXX ...

* Changes for developers:

  * As part of VRT version 7.0, the ``path`` field has been added to
    to ``struct vrt_backend``, which a VMOD can use with
    ``VRT_new_backend()`` to create a dynamic backend with a UDS
    address (see ``vrt.h``).

    If ``path`` is non-NULL, then both of the IPv4 and IPv6 addresses
    must be NULL. If ``path`` is NULL, then (as before) one or both of
    the IP addresses must be non-NULL. The ``dyn_uds`` object in VMOD
    debug (available in the source tree) illustrates how this can be
    done.

  * VMOD vcc sources may now include a directive ``$Synopsis`` whose
    value may be ``auto`` or ``manual``, default ``auto``.

    When ``$Synopsis`` is ``auto``, the vmodtool generates a more
    comprehensive ``SYNOPSIS`` section in the documentation than in
    previous versions -- an overview of the objects, methods and
    functions in your VMOD, with their type signatures.

    When ``$Synopsis`` is ``manual``, the ``SYNOPSIS`` is left out of
    the generated docs altogether; so you can write the ``SYNOPSIS``
    section yourself, if you prefer.

  * XXX ...

*eof*
