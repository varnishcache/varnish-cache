..
	Copyright (c) 2012-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _users-guide-command-line:

Required command line arguments
-------------------------------

There only one command line argument you have to provide when starting Varnish,
which is '-b' for where the backend server can be contacted.

'-a' is another argument which is likely to require adjustment.

If you have installed Varnish through using a provided operating system bound package,
you will find the startup options here:

* Debian, Ubuntu: `/etc/default/varnish`
* Red Hat, Centos: `/etc/sysconfig/varnish`
* FreeBSD: `/etc/rc.conf` (See also: /usr/local/etc/rc.d/varnishd)


'-a' *<[name=][%kind,][listen_address[,PROTO|,option=value,...]]>*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Each '-a' argument defines one endpoint which Varnish should service HTTP
requests on.

The default is ``:80,http`` to listen on the Well Known Port for HTTP. If your
webserver runs on the same machine, you will likely have to move it to another
port number or bind it to a loopback address first.

Multiple '-a' arguments can be provided to service multiple endpoints. *name* is
the ``local.socket`` name for VCL. *listen_address* can be an IPv4 or IPv6
address with a port, a unix domain socket path or an abstract socket. See
:ref:`varnishd(1)` for more details.

Here are some examples::

	-a http=:80
	-a localhost:80,HTTP
	-a 192.168.1.100:8080
	-a '[fe80::1]:80'
	-a '0.0.0.0:8080,[::]:8081'
        -a uds=/my/path,PROXY,mode=666
        -a @abstract_socket


'-f' *VCL-file* or '-b' *backend*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Varnish needs to know where to find the HTTP server it is caching for.
You can either specify it with the '-b' argument, or you can put it in your own VCL file, specified with the '-f' argument.

Using '-b' is a quick way to get started::

	-b localhost:81
	-b thatotherserver.example.com:80
	-b 192.168.1.2:80

Notice that if you specify a name, it can at most resolve to one IPv4
*and* one IPv6 address.

For more advanced use, you will want to specify a VCL program with ``-f``,
but you can start with as little as just::

	backend default {
		.host = "localhost:81";
	}

which is, by the way, *precisely* what '-b' does.

Optional arguments
^^^^^^^^^^^^^^^^^^

For a complete list of the command line arguments please see
:ref:`varnishd(1) options <ref-varnishd-options>`.
