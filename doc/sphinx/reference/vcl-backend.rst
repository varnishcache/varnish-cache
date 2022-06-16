..
	Copyright (c) 2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. role:: ref(emphasis)

.. _vcl-backend(7):

============
VCL-backends
============

--------------------
Configuring Backends
--------------------

:Manual section: 7

.. _backend_definition:

Backend definition
------------------

A backend declaration creates and initialises a named backend object.
A declaration start with the keyword ``backend`` followed by the name of the
backend. The actual declaration is in curly brackets, in a key/value fashion.::

    backend name {
        .attribute1 = value;
        .attribute2 = value;
	[...]
    }

If there is a backend named ``default`` it will be used unless another
backend is explicitly set.  If no backend is named ``default`` the first
backend in the VCL program becomes the default.

If you only use dynamic backends created by VMODs, an empty, always failing
(503) backend can be specified::

  backend default none;

A backend must be specified with either a ``.host`` or a ``.path`` attribute, but
not both.  All other attributes have default values.

Attribute ``.host``
-------------------

To specify a networked backend ``.host`` takes either a numeric
IPv4/IPv6 address or a domain name which resolves to *at most*
one IPv4 and one IPv6 address::

    .host = "127.0.0.1";

    .host = "[::1]:8080";

    .host = "example.com:8081";

    .host = "example.com:http";

Attribute ``.port``
-------------------

The TCP port number or service name can be specified as part of
``.host`` as above or separately using the ``.port`` attribute::

    .port = "8081";

    .port = "http";

Attribute ``.path``
-------------------

The absolute path to a Unix(4) domain socket of a local backend::

    .path = "/var/run/http.sock";

A warning will be issued if the uds-socket does not exist when the
VCL is loaded.  This makes it possible to start the UDS-listening peer,
or set the socket file's permissions afterwards.

If the uds-socket socket does not exist or permissions deny access,
connection attempts will fail.

Attribute ``.host_header``
--------------------------

A host header to add to probes and regular backend requests if they have no such header::

    .host_header = "Host: example.com";

Timeout Attributes
------------------

These attributes control how patient `varnishd` is during backend fetches::

    .bereq_connect_timeout = 1.4s;
    .beresp_start_timeout = 20s;
    .beresp_idle_timeout = 10s;

The default values comes parameters with the same names, see :ref:`varnishd(1)`.

Attribute ``.max_connections``
------------------------------

Limit how many simultaneous connections varnish can open to the backend::

    .max_connections = 1000;

Attribute ``.proxy_header``
---------------------------

Send a PROXY protocol header to the backend with the ``client.ip`` and
``server.ip`` values::

    .proxy_header = 2;

Legal values are one and two, depending which version of the PROXY protocol you want.

*Notice* this setting will lead to backend connections being used
for a single request only (subject to future improvements). Thus,
extra care should be taken to avoid running into failing backend
connections with EADDRNOTAVAIL due to no local ports being
available. Possible options are:

    * Use additional backend connections to extra IP addresses or TCP ports

    * Increase the number of available ports (Linux sysctl ``net.ipv4.ip_local_port_range``)

    * Reuse backend connection ports early (Linux sysctl ``net.ipv4.tcp_tw_reuse``)

Attribute ``.preamble``
-----------------------

Send a BLOB on all newly opened connections to the backend::

    .preamble = :SGVsbG8gV29ybGRcbgo=:;

Attribute ``.probe``
--------------------

Please see :ref:`vcl-probe(7)`.

SEE ALSO
--------

* :ref:`varnishd(1)`
* :ref:`vcl(7)`
* :ref:`vcl-probe(7)`
* :ref:`vmod_directors(3)`
* :ref:`vmod_std(3)`

HISTORY
-------

VCL was developed by Poul-Henning Kamp in cooperation with Verdens
Gang AS, Redpill Linpro and Varnish Software.  This manual page is
written by Per Buer, Poul-Henning Kamp, Martin Blix Grydeland,
Kristian Lyngstøl, Lasse Karstensen and others.

COPYRIGHT
---------

This document is licensed under the same license as Varnish
itself. See LICENSE for details.

* Copyright (c) 2006 Verdens Gang AS
* Copyright (c) 2006-2021 Varnish Software AS
