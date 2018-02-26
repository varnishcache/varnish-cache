.. _whatsnew_upgrading_6.0:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 6.0
%%%%%%%%%%%%%%%%%%%%%%%%

XXX: Most important change first
================================

XXX ...

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

XXX VCL subhead 1
~~~~~~~~~~~~~~~~~

XXX: ... etc.

VCL variables
~~~~~~~~~~~~~

XXX: VCL vars subhead 1
-----------------------

XXX: ...

XXX: VCL vars subhead 2
-----------------------

XXX: ...

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
    issues with permissions. No further attempted is made to set the
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
