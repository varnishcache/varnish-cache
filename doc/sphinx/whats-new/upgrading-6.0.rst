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

* ``varnishd(1)``:

  * XXX ...

  * XXX ...

* ``varnishstat(1)``:

  * XXX ...

  * XXX ...

* ``varnishlog(1)``:

  * Added a third field to the ``ReqStart`` log record that contains the
    name of the listener address over which the request was received, see
    :ref:`vsl(7)`.

  * XXX ...

* Changes for developers:

  * XXX ...

  * XXX ...

*eof*
