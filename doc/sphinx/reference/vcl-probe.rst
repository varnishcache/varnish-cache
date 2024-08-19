..
	Copyright (c) 2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. role:: ref(emphasis)

.. _vcl-probe(7):

=========
VCL-probe
=========

---------------------------------
Configuring Backend Health Probes
---------------------------------

:Manual section: 7

.. _reference-vcl_probes:

Backend health probes
---------------------

Varnish can be configured to periodically send a request to test if a
backend is answering and thus "healthy".

Probes can be configured per backend::

    backend foo {
        [...]
        .probe = {
            [...]
        }
    }

They can be named and shared between backends::

    probe light {
        [...]
    }

    backend foo {
        .probe = light;
    }

    backend bar {
        .probe = light;
    }

Or a ``default`` probe can be defined, which applies to all backends
without a specific ``.probe`` configured::

    probe default {
        [...]
    }

The basic syntax is the same as for backends::

    probe name {
        .attribute1 = value;
        .attribute2 = "value";
        [...]
    }

There are no mandatory attributes, they all have defaults.

Attribute ``.url``
------------------

The URL to query.  Defaults to ``/``::

    .url = "/health-probe";

Attribute ``.request``
----------------------

Can be used to specify a full HTTP/1.1 request to be sent::

    .request = "GET / HTTP/1.1"
        "Host: example.com"
        "X-Magic: We're fine with this."
        "Connection: close";

Each of the strings will have ``CRLF`` appended and a final HTTP
header block terminating ``CRLF`` will be appended as well.

Because connection shutdown is part of the health check,
``Connection: close`` is mandatory.

Attribute ``.expected_response``
--------------------------------

The expected HTTP status, defaults to ``200``::

    .expected_response = 418;

Attribute ``.expect_close``
---------------------------

Whether or not to expect the backend to close the underlying connection.

Accepts ``true`` or ``false``, defaults to ``true``::

    .expect_close = false;

Warning: when the backend does not close the connection,
setting ``expect_close`` to ``false`` makes probe tasks wait until
they time out before inspecting the response.

Attribute ``.timeout``
----------------------

How fast the probe must succeed, default is two seconds::

    .timeout = 10s;

Attribute ``.interval``
-----------------------

Time between probes, default is five seconds::

    .interval = 1m;

The backend health shift register
---------------------------------

Backend health probes uses a 64 stage shift register to remember the
most recent health probes and to evaluate the total health of the backend.

In the CLI, a good backend health status looks like this:

.. code-block:: text

    varnish> backend.list -p boot.backend
    Backend name    Admin    Probe    Health     Last change
    boot.backend    probe    5/5      healthy    Wed, 13 Jan 2021 10:31:50 GMT
     Current states  good:  5 threshold:  4 window:  5
      Average response time of good probes: 0.000793
      Oldest ================================================== Newest
      4444444444444444444444444444444444444444444444444444444444444444 Good IPv4
      XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX Good Xmit
      RRRRRRRRRRRRRRRRRRRRRRR----RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR Good Recv
      HHHHHHHHHHHHHHHHHHHHHHH--------HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH Happy

Starting from the bottom, the last line shows that this backend has been
declared "Happy" for most the 64 health probes, but there were some
trouble some while ago.

However, in this case the ``.window`` is configured to five, and the
``.threshold`` is set to four, so at this point in time, the backend
is considered fully healthy.

An additional ``.initial`` fills that many "happy" entries in the
shift register when the VCL is loaded, so that backends can quickly
become healthy, even if their health is normally considered over
many samples::

    .interval = 1s;
    .window = 60;
    .threshold = 45;
    .initial = 43;

This backend will be considered healthy if three out of four health
probes in the last minute were good, but it becomes healthy as soon
as two good probes have happened after the VCL was loaded.

The default values are:

* ``.window`` = 8

* ``.threshold`` = 3

* ``.initial`` = one less than ``.threshold``

Note that the default ``.initial`` means that the backend will be marked
unhealthy until the first probe response come back successful.
This means that for backends created on demand (by vmods) cannot use the
default value for ``.initial``, as the freshly created backend would very
likely still be unhealthy when the backend request happens.

SEE ALSO
========

* :ref:`varnishd(1)`
* :ref:`vcl(7)`
* :ref:`vcl-backend(7)`
* :ref:`vmod_directors(3)`
* :ref:`vmod_std(3)`

HISTORY
=======

VCL was developed by Poul-Henning Kamp in cooperation with Verdens
Gang AS, Redpill Linpro and Varnish Software.  This manual page is
written by Per Buer, Poul-Henning Kamp, Martin Blix Grydeland,
Kristian Lyngstøl, Lasse Karstensen and others.

COPYRIGHT
=========

This document is licensed under the same license as Varnish
itself. See LICENSE for details.

* Copyright (c) 2006 Verdens Gang AS
* Copyright (c) 2006-2021 Varnish Software AS
