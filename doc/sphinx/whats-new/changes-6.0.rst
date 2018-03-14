.. _whatsnew_changes_6.0:

Changes in Varnish 6.0
======================

Usually when we do dot-zero releases in Varnish, it means that
users are in for a bit of work to upgrade to the new version,
but 6.0 is actually not that scary, because most of the changes
are either under the hood or entirely new features.

The biggest user-visible change is probably that we, or to be totally
honest here: Geoff Simmons, have added support for Unix Domain
Sockets, both for clients and backend servers.

Because UNIX Domain Sockets have nothing like IP numbers, we were
forced to define a new level of the VCL language ``vcl 4.1`` to
cope with UDS.

Both ``vcl 4.0`` and ``vcl 4.1`` are supported, and it is the primary
source-file which controls which it will be, and you can ``include``
lower versions, but not higher versions that that.

Some old variables are not available in 4.1 and some new variables
are not available in 4.0.  Please see :ref:`vcl_variables` for
specifics.

There are a few other changes to the ``vcl 4.0``, most notably that
we now consider upper- and lower-case the same for symbols.

There are new and improved VMODs:

* :ref:`vmod_purge(3)` -- fine-grained and "soft" purges

* :ref:`vmod_unix(3)` -- Unix Domain Socket information

* :ref:`vmod_blob(3)` -- Handling of binary blobs (think: Cookies)

* :ref:`vmod_proxy(3)` -- Proxy protocol information

* :ref:`vmod_vtc(3)` -- Utility functions for writing :ref:`varnishtest(1)` cases.



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
