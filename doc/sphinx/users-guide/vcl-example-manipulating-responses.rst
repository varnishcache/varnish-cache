..
	Copyright (c) 2013-2017 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license



Altering the backend response
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Here we override the TTL of a object coming from the backend if it
matches certain criteria::

  sub vcl_backend_response {
     if (bereq.url ~ "\.(png|gif|jpg)$") {
       unset beresp.http.set-cookie;
       set beresp.ttl = 1h;
    }
  }



We also remove any Set-Cookie headers in order to avoid creation of a
`hit-for-miss` object. See :ref:`vcl_actions`.
