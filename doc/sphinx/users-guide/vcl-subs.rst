
vcl_recv
~~~~~~~~

vcl_recv (yes, we're skimpy with characters, it's Unix) is called at
the beginning of a request, after the complete request has been
received and parsed.  Its purpose is to decide whether or not to serve
the request, how to do it, and, if applicable, which backend to use.

In vcl_recv you can also alter the request. Typically you can alter
the cookies and add and remove request headers.

Note that in vcl_recv only the request object, req is available.

vcl_fetch
~~~~~~~~~

vcl_fetch is called *after* a document has been successfully retrieved
from the backend. Normal tasks her are to alter the response headers,
trigger ESI processing, try alternate backend servers in case the
request failed.

In vcl_fetch you still have the request object, req, available. There
is also a *backend response*, beresp. beresp will contain the HTTP
headers from the backend.
