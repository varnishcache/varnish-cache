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

And clients which do not support gzip gets their Accept-Encoding
header removed.  This ensures conformity with respect to creating
Vary: strings during object creation.

During lookup, we ignore any "Accept-encoding" in objects Vary: strings,
to avoid having a gzip and gunzip'ed version of the object, varnish
can gunzip on demand.  (We implement this bit of magic at lookup time,
so that any objects stored in persistent storage can be used with
or without gzip support enabled.)

Varnish will not do any other types of compressions than gzip, in particular
we will not do deflate, as there are browser bugs in that case.

Before vcl_miss{} is called, the backend requests Accept-Encoding is
always set to:

	Accept-Encoding: gzip

Even if this particular client does not support 

To always entice the backend into sending us gzip'ed content.

Varnish will not gzip any content on its own (but see below), we trust
the backend to know what content can be sensibly gzip'ed (html) and what
can not (jpeg)

If in vcl_backend_response{} we find out that we are trying to deliver a
gzip'ed object to a client that has not indicated willingness to receive
gzip, we will ungzip the object during deliver.

Tuning, tweaking and frobbing
-----------------------------

In vcl_recv{} you have a chance to modify the client's
Accept-Encoding: header before anything else happens.

In vcl_pass{} the clients Accept-Encoding header is copied to the
backend request unchanged.
Even if the client does not support gzip, you can force the A-C header
to "gzip" to save bandwidth between the backend and varnish, varnish will
gunzip the object before delivering to the client.

In vcl_miss{} you can remove the "Accept-Encoding: gzip" header, if you
do not want the backend to gzip this object.

In vcl_backend_response{} two new variables allow you to modify the
gzip-ness of objects during fetch:

	set beresp.do_gunzip = true;

Will make varnish gunzip an already gzip'ed object from the backend during
fetch.  (I have no idea why/when you would use this...)

	set beresp.do_gzip = true;

Will make varnish gzip the object during fetch from the backend, provided
the backend didn't send us a gzip'ed object.

Remember that a lot of content types cannot sensibly be gziped, most
notably compressed image formats like jpeg, png and similar, so a
typical use would be::

	sub vcl_backend_response {
		if (bereq.url ~ "html$") {
			set beresp.do_gzip = true;
		}
	}

GZIP and ESI
------------

First, note the new syntax for activating ESI::

	sub vcl_backend_response {
		set beresp.do_esi = true;
	}

In theory, and hopefully in practice, all you read above should apply also
when you enable ESI, if not it is a bug you should report.

But things are vastly more complicated now.  What happens for
instance, when the backend sends a gzip'ed object we ESI process
it and it includes another object which is not gzip'ed, and we want
to send the result gziped to the client ?

Things can get really hairy here, so let me explain it in stages.

Assume we have a ungzipped object we want to ESI process.

The ESI parser will run through the object looking for the various
magic strings and produce a byte-stream we call the "VEC" for Varnish
ESI Codes.

The VEC contains instructions like "skip 234 bytes", "deliver 12919 bytes",
"include /foobar", "deliver 122 bytes" etc and it is stored with the
object.

When we deliver an object, and it has a VEC, special esi-delivery code
interprets the VEC string and sends the output to the client as ordered.

When the VEC says "include /foobar" we do what amounts to a restart with
the new URL and possibly Host: header, and call vcl_recv{} etc.  You
can tell that you are in an ESI include by examining the 'req.esi_level'
variable in VCL.

The ESI-parsed object is stored gzip'ed under the same conditions as
above:  If the backend sends gzip'ed and VCL did not ask for do_gunzip,
or if the backend sends ungzip'ed and VCL asked for do_gzip.

Please note that since we need to insert flush and reset points in
the gzip file, it will be slightly larger than a normal gzip file of
the same object.

When we encounter gzip'ed include objects which should not be, we
gunzip them, but when we encounter gunzip'ed objects which should
be, we gzip them, but only at compression level zero.

So in order to avoid unnecessary work, and in order to get maximum
compression efficiency, you should::

	sub vcl_miss {
		if (object needs ESI processing) {
			unset bereq.http.accept-encoding;
		}
	}

	sub vcl_backend_response {
		if (object needs ESI processing) {
			set beresp.do_esi = true;
			set beresp.do_gzip = true;
		}
	}

So that the backend sends these objects uncompressed to varnish.

You should also attempt to make sure that all objects which are
esi:included are gziped, either by making the backend do it or
by making varnish do it.
