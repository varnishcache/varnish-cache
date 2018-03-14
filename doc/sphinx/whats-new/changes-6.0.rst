.. _whatsnew_changes_6.0:

Changes in Varnish 6.0
======================

XXX: ... intro paragraphs ...

.. _whatsnew_new_subhead_1:

XXX subhead 1
~~~~~~~~~~~~~

XXX ...

XXX subsubhead 1.1
------------------

XXX: ...

XXX subsubhead 1.2
------------------

XXX: ...

.. _whatsnew_new_uds:

Unix domain sockets as listen and backend addresses
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you are using VCL 4.1, the ``varnishd -a`` command-line argument
allows you to specify Unix domain sockets as listener addresses
(beginning with ``/``, see varnishd :ref:`ref-varnishd-options`)::

  varnishd -a /path/to/listen.sock,PROXY,user=vcache,group=varnish,mode=660

The ``user``, ``group`` and ``mode`` sub-arguments set the permissions
of the newly created socket file.

A backend declaration in VCL 4.1 may now include the ``.path`` field
(instead of ``.host``) for the absolute path of a Unix domain socket
to which Varnish connects::

  backend uds {
  	.path = "/path/to/backend.sock";
  }

This of course can only be used to communicate with other processes on
the same host, if they also support socket file addresses. Until now,
connections with other process co-located with Varnish were only
possible over locally available IP addresses, such as loopback. Unix sockets
may have some advantages for such a configuration:

* Network traffic over Unix sockets does not have the overhead of the
  TCP stack. You may see a significant improvement in throughput
  compared to using the loopback address.

* The file permission system can be used to impose restrictions on the
  processes that can connect to Varnish, and the processes to which
  Varnish can connect.

* Using paths as addresses may be easier to configure than searching
  for open port numbers on loopback, especially for automated
  configurations.

The obvious use cases are SSL offloaders that decrypt traffic from the
network and pass requests on to Varnish, and SSL "onloaders" that
encrypt backend traffic from Varnish and forward requests over
untrusted networks. But this should be useful for any configuration in
which Varnish talks to processes on the same host.

The distribution has a new :ref:`VMOD unix <vmod_unix(3)>` that you
may be able to use in VCL to get information about the credentials of
a process (user and group of the process owner) that is connected to a
Varnish listener over a Unix socket. This is not supported on every
platform, so check the VMOD docs to see if it will work for you.

XXX subsubhead 2.1
------------------

XXX: ...

News for authors of VMODs and Varnish API client applications
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. _whatsnew_dev_subhead_1:

XXX dev subhead 1
-----------------

XXX ...

.. _whatsnew_dev_subhead_2:

XXX dev subhead 2
-----------------

XXX ...

*eof*
