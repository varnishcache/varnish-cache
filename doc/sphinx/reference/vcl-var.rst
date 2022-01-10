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
