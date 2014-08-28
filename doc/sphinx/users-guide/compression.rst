.. _users-guide-compression:

Compression
-----------

In Varnish 3.0 we introduced native support for compression, using gzip
encoding. *Before* 3.0, Varnish would never compress objects. 

In Varnish 4.0 compression defaults to "on", meaning that it tries to
be smart and do the sensible thing.

If you don't want Varnish tampering with the encoding you can disable
compression all together by setting the parameter `http_gzip_support` to
false. Please see man :ref:`ref-varnishd` for details.

Default behaviour
~~~~~~~~~~~~~~~~~

The default behaviour is active when the `http_gzip_support` parameter
is set to "on" and neither `beresp.do_gzip` nor `beresp.do_gunzip` are
used in VCL.

Unless returning from `vcl_recv` with `pipe` or `pass`, Varnish
modifies `req.http.Accept-Encoding`: if the client supports gzip
`req.http.Accept-Encoding` is set to "gzip", otherwise the header is
removed.

Unless the request is a `pass`, Varnish sets `bereq.http.Accept-Encoding`
to "gzip" before `vcl_backend_fetch` runs, so the header can be changed
in VCL.

If the server responds with gzip'ed content it will be stored in memory
in its compressed form and `Accept-Encoding` will be added to the
`Vary` header.

To clients supporting gzip, compressed content is delivered unmodified.

For clients not supporting gzip, compressed content gets decompressed
on the fly while delivering it. The `Content-Encoding` response header
gets removed and any `Etag` gets weakened (by prepending "W/").

For Vary Lookups, `Accept-Encoding` is ignored.

Compressing content if backends don't
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can tell Varnish to compress content before storing it in cache in
`vcl_backend_response` by setting `beresp.do_gzip` to "true", like this::

    sub vcl_backend_response {
        if (beresp.http.content-type ~ "text") {
            set beresp.do_gzip = true;
        }
    }

With `beresp.do_gzip` set to "true", Varnish will make the following
changes to the headers of the resulting object before inserting it in
the cache:

* set `obj.http.Content-Encoding` to "gzip"
* add "Accept-Encoding" to `obj.http.Vary`, unless already present
* weaken any `Etag` (by prepending "W/")

Generally, Varnish doesn't use much CPU so it might make more sense to
have Varnish spend CPU cycles compressing content than doing it in your
web- or application servers, which are more likely to be CPU-bound.

Please make sure that you don't try to compress content that is
uncompressable, like JPG, GIF and MP3 files. You'll only waste CPU cycles.

Uncompressing content before entering the cache
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can also uncompress content before storing it in cache by setting
`beresp.do_gunzip` to "true". One use case for this feature is to work
around badly configured backends uselessly compressing already compressed
content like JPG images (but fixing the misbehaving backend is always
the better option).

With `beresp.do_gunzip` set to "true", Varnish will make the following
changes to the headers of the resulting object before inserting it in
the cache:

* remove `obj.http.Content-Encoding`
* weaken any `Etag` (by prepending "W/")

.. XXX pending closing #940: remove any "Accept-Encoding" from `obj.http.Vary`

GZIP and ESI
~~~~~~~~~~~~

If you are using Edge Side Includes (ESI) you'll be happy to note that
ESI and GZIP work together really well. Varnish will magically decompress
the content to do the ESI-processing, then recompress it for efficient
storage and delivery.

Turning off gzip support
~~~~~~~~~~~~~~~~~~~~~~~~

When the `http_gzip_support` parameter is set to "off", Varnish does
not do any of the header alterations documented above, handles `Vary:
Accept-Encoding` like it would for any other `Vary` value and ignores
`beresp.do_gzip` and `beresp.do_gunzip`.

A random outburst
~~~~~~~~~~~~~~~~~

Poul-Henning Kamp has written :ref:`phk_gzip` which talks a bit more
about how the implementation works.
