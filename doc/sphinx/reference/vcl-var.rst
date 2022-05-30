..
	Copyright (c) 2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. role:: ref(emphasis)

.. _vcl-var(7):

=============
VCL-Variables
=============

------------------
The complete album
------------------

:Manual section: 7

DESCRIPTION
===========

This is a list of all variables in the VCL language.

Variable names take the form ``scope.variable[.index]``, for instance::

	req.url
	beresp.http.date
	client.ip

Which operations are possible on each variable is described below,
often with the shorthand "backend" which covers the ``vcl_backend_* {}``
subroutines and "client" which covers the rest, except ``vcl_init {}``
and ``vcl_fini {}``.

.. include:: vcl_var.rst

.. _protected_headers:

Protected header fields
-----------------------

The ``content-length`` and ``transfer-encoding`` headers are read-only. They
must be preserved to ensure HTTP/1 framing remains consistent and maintain a
proper request and response synchronization with both clients and backends.

VMODs can still update these headers, when there is a reason to change the
framing, such as a transformation of a request or response body.

HTTP response status
--------------------

A HTTP status code has 3 digits XYZ where X must be between 1 and 5 included.
Since it is not uncommon to see HTTP clients or servers relying
on non-standard or even invalid status codes, Varnish can work
with any status between 100 and 999.

Within VCL code it is even possible to use status codes in the form
VWXYZ as long as the overall value is lower than 65536, but only
the XYZ part will be sent to the client, by which time the X must
also have become non-zero.

The VWXYZ form of status codes can be communicate extra information
in ``resp.status`` and ``beresp.status`` to ``return(synth(...))`` and
``return(error(...))``, to indicate which synthetic content to produce::

    sub vcl_recv {
        if ([...]) {
            return synth(12404);
        }
    }

    sub vcl_synth {
        if (resp.status == 12404) {
            [...] 	// this specific 404
        } else if (resp.status % 1000 == 404) {
            [...] 	// all other 404's
        }
    }

The ``obj.status`` variable will inherit the VWXYZ form, but in a ban
expression only the XYZ part will be available. The VWXYZ form is strictly
limited to VCL execution.

Assigning an HTTP standardized code to ``resp.status`` or ``beresp.status``
will also set ``resp.reason`` or ``beresp.reason``  to the corresponding
status message.


304 handling
~~~~~~~~~~~~

For a 304 response, Varnish core code amends ``beresp`` before calling
`vcl_backend_response`:

* If the gzip status changed, ``Content-Encoding`` is unset and any
  ``Etag`` is weakened

* Any headers not present in the 304 response are copied from the
  existing cache object. ``Content-Length`` is copied if present in
  the existing cache object and discarded otherwise.

* The status gets set to 200.

`beresp.was_304` marks that this conditional response processing has
happened.

Note: Backend conditional requests are independent of client
conditional requests, so clients may receive 304 responses no matter
if a backend request was conditional.

beresp.ttl / beresp.grace / beresp.keep
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Before calling `vcl_backend_response`, core code sets ``beresp.ttl``
based on the response status and the response headers ``Age``,
``Cache-Control`` or ``Expires`` and ``Date`` as follows:

* If present and valid, the value of the ``Age`` header is effectively
  deduced from all ttl calculations.

* For status codes 200, 203, 204, 300, 301, 304, 404, 410 and 414:

  * If ``Cache-Control`` contains an ``s-maxage`` or ``max-age`` field
    (in that order of preference), the ttl is set to the respective
    non-negative value or 0 if negative.

  * Otherwise, if no ``Expires`` header exists, the default ttl is
    used.

  * Otherwise, if ``Expires`` contains a time stamp before ``Date``,
    the ttl is set to 0.

  * Otherwise, if no ``Date`` header is present or the ``Date`` header
    timestamp differs from the local clock by no more than the
    `clock_skew` parameter, the ttl is set to

    * 0 if ``Expires`` denotes a past timestamp or

    * the difference between the local clock and the ``Expires``
      header otherwise.

  * Otherwise, the ttl is set to the difference between ``Expires``
    and ``Date``

* For status codes 302 and 307, the calculation is identical except
  that the default ttl is not used and -1 is returned if neither
  ``Cache-Control`` nor ``Expires`` exists.

* For all other status codes, ttl -1 is returned.

``beresp.grace`` defaults to the `default_grace` parameter.

For a non-negative ttl, if ``Cache-Control`` contains a
``stale-while-revalidate`` field value, ``beresp.grace`` is
set to that value if non-negative or 0 otherwise.

``beresp.keep`` defaults to the `default_keep` parameter.



SEE ALSO
========

* :ref:`varnishd(1)`
* :ref:`vcl(7)`

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
