.. _tutorial-vary:

Vary
~~~~

The Vary header is sent by the web server to indicate what makes a
HTTP object Vary. This makes a lot of sense with headers like
Accept-Encoding. When a server issues a "Vary: Accept-Encoding" it
tells Varnish that its needs to cache a separate version for every
different Accept-Encoding that is coming from the clients. So, if a
clients only accepts gzip encoding Varnish won't serve the version of
the page encoded with the deflate encoding.

The problem is that the Accept-Encoding field contains a lot of
different encodings. If one browser sends::

  Accept-Encodign: gzip,deflate

And another one sends::

  Accept-Encoding:: deflate,gzip

Varnish will keep two variants of the page requested due to the
different Accept-Encoding headers. Normalizing the accept-encoding
header will sure that you have as few variants as possible. The
following VCL code will normalize the Accept-Encoding headers.::

    if (req.http.Accept-Encoding) {
        if (req.url ~ "\.(jpg|png|gif|gz|tgz|bz2|tbz|mp3|ogg)$") {
            # No point in compressing these
            remove req.http.Accept-Encoding;
        } elsif (req.http.Accept-Encoding ~ "gzip") {
            set req.http.Accept-Encoding = "gzip";
        } elsif (req.http.Accept-Encoding ~ "deflate") {
            set req.http.Accept-Encoding = "deflate";
        } else {
            # unkown algorithm
            remove req.http.Accept-Encoding;
        }
    }

The code sets the Accept-Encoding header from the client to either
gzip, deflate with a preference for gzip.

Pitfall - Vary: User-Agent
~~~~~~~~~~~~~~~~~~~~~~~~~~

Some applications or application servers send *Vary: User-Agent* along
with their content. This instructs Varnish to cache a separate copy
for every variation of User-Agent there is. There are plenty. Even a
single patchlevel of the same browser will generate at least 10
different User-Agent headers based just on what operating system they
are running. 

So if you *really* need to Vary based on User-Agent be sure to
normalize the header or your hit rate will suffer badly. Use the above
code as a template.

