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

``sess.xid``
------------

This is the unique ID assigned by Varnish to the current session,
which stands for the "conversation" with a single client connection
that comprises one or more request/response transactions. It is the
same XID shown in the log for session transactions (with
``-g session`` grouping). ``sess.xid`` is read-only and is available
as of VCL 4.1.

XXX: VCL vars subhead 2
-----------------------

XXX: ...

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

VMOD std
--------

:ref:`std.port(IP) <func_port>` always returns 0 when applied to a
``*.ip`` variable whose value is set to ``0.0.0.0`` because the
listener is UDS.  :ref:`std.set_ip_tos(INT) <func_set_ip_tos>` is
silently ignored when the listener is UDS.

XXX VCL subhead 2
~~~~~~~~~~~~~~~~~

XXX: ...

Other changes
=============

* ``varnishlog(1)``:

  * Added a third field to the ``ReqStart`` log record that contains the
    name of the listener address over which the request was received, see
    :ref:`vsl(7)`.

  * XXX ...

* ``varnishtest(1)``:

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

    A convenient location for socket files to be used in a test is the
    temporary directory created by ``varnishtest`` for each test,
    whose path is held in the macro ``${tmpdir}``. So this is a common
    idiom for tests that involve UDSen::

      varnish v1 -arg "-a ${tmpdir}/v1.sock" -vcl { ... } -start

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

  * XXX ...

  * XXX ...

*eof*
