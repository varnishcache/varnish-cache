.. _phk_gzip:

=======================================
How GZIP, and GZIP+ESI works in Varnish
=======================================

First of all, everything you read about GZIP here, is controlled by the
parameter:

	http_gzip_support

Which defaults to "on" if you do not want Varnish to try to be smart
about compression, set it to "off" instead.

What does http_gzip_support do
------------------------------

A request which is sent into 'pipe' or 'pass' mode from vcl_recv{}
will not experience any difference, this processing only affects
cache hit/miss requests.

Unless vcl_recv{} results in "pipe" or "pass", we determine if the
client is capable of receiving gzip'ed content.  The test amounts to:

	Is there a Accept-Encoding header that mentions gzip, and if
	is has a q=# number, is it larger than zero.

Clients which can do gzip, gets their header rewritten to:

	Accept-Encoding: gzip

And clients which do not support gzip gets their Accept-Encoding header
removed.  This ensures conformity with respect to Vary: strings during
hash lookup.

Varnish will not do any other types of compressions than gzip, in particular
we will not do deflate, as there are browser bugs in that case.

Before vcl_hit{} is called, the backend requests Accept-Encoding is
always set to:

	Accept-Encoding: gzip

To always entice the backend into sending us gzip'ed content.

Varnish will not gzip any content on its own (but see below), we trust
the backend to know what content can be sensibly gzip'ed (html) and what
can not (jpeg)

If in vcl_fetch{} we find out that we are trying to deliver a gzip'ed object
to a client that has not indicated willingness to receive gzip, we will
ungzip the object during deliver.

Tuning, tweaking and frobbing
-----------------------------

In vcl_recv{} you have a chance t modify the clients Accept-Encoding: header
before anything else happens.

In vcl_pass{} the clients Accept-Encoding header is copied to the
backend request unchanged.
Even if the client does not support gzip, you can force the A-C header
to "gzip" to save bandwidth between the backend and varnish, varnish will
gunzip the object before delivering to the client.

In vcl_fetch{} two new variables allow you to modify the gzip-ness of
objects during fetch:

	set beresp.do_gunzip = true;

Will make varnish gunzip an already gzip'ed object from the backend during
fetch.  (I have no idea why/when you would use this...)

	set beresp.do_gzip = true;

Will make varnish gzip the object during fetch from the backend, provided
the backend didn't send us a gzip'ed object.

Remember that a lot of content types cannot sensibly be gziped, most
notably compressed image formats like jpeg, png and similar, so a
typical use would be:

	sub vcl_fetch {
		if (req.url ~ "html$") {
			set beresp.do_gzip = true;
		}
	}

GZIP and ESI
------------

TBD.
