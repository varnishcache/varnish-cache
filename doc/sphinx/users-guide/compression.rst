.. _users-guide-compression:

Compression
-----------

New in Varnish 3.0 was native support for compression, using gzip
encoding. *Before* 3.0, Varnish would never compress objects. 

In Varnish 3.0 compression defaults to "on", meaning that it tries to
be smart and do the sensible thing.

If you don't want Varnish tampering with the encoding you can disable
compression all together by setting the parameter http_gzip_support to
*false*. Please see man :ref:`ref-varnishd` for details.


Default behaviour
~~~~~~~~~~~~~~~~~

The default for Varnish is to check if the client supports our
compression scheme (gzip) and if it does it will override the
Accept-Encoding header and set it to "gzip".

When Varnish then issues a backend request the Accept-Encoding will
then only consist of "gzip". If the server responds with gzip'ed
content it will be stored in memory in its compressed form. If the
backend sends content in clear text it will be stored like that.

You can make Varnish compress content before storing it in cache in
vcl_fetch by setting do_gzip to true, like this::

   sub vcl_backend_response {
        if (beresp.http.content-type ~ "text") {
                set beresp.do_gzip = true;
        }
  }

Please make sure that you don't try to compress content that is
uncompressable, like jpgs, gifs and mp3. You'll only waste CPU
cycles. You can also uncompress objects before storing it in memory by
setting do_gunzip to *true* but I have no idea why anybody would want
to do that.

Generally, Varnish doesn't use much CPU so it might make more sense to
have Varnish spend CPU cycles compressing content than doing it in
your web- or application servers, which are more likely to be
CPU-bound.

GZIP and ESI
~~~~~~~~~~~~

If you are using Edge Side Includes you'll be happy to note that ESI
and GZIP work together really well. Varnish will magically decompress
the content to do the ESI-processing, then recompress it for efficient
storage and delivery. 


Clients that don't support gzip
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If the client does not support gzip the Accept-Encoding header is left
alone and we'll end up serving whatever we get from the backend
server. Remember that the Backend might tell Varnish to *Vary* on the
Accept-Encoding.

If the client does not support gzip but we've already got a compressed
version of the page in memory Varnish will automatically decompress
the page while delivering it.


A random outburst
~~~~~~~~~~~~~~~~~

Poul has written :ref:`phk_gzip` which talks abit more about how the
implementation works. 
