#
# This is an example VCL file for Varnish.
#
# It does not do anything by default, delegating control to the builtin vcl.
# The builtin VCL is called when there is no explicit explicit return
# statement.
#
# See the VCL tutorial at https://www.varnish-cache.org/docs/trunk/tutorial/
# and http://varnish-cache.org/trac/wiki/VCLExamples for more examples.

# Marker to tell the VCL compiler that this VCL has been adapted to the
# new 4.0 format.
vcl 4.0;

# Default backend definition. Set this to point to your content server.
backend default {
    .host = "127.0.0.1";
    .port = "8080";
}

sub vcl_recv {
    # Happens before we check if we have this in cache already.
    # See http://www.varnish-cache.org/docs/3.0/tutorial/vcl.html#vcl_recv
}

sub vcl_backend_response {
    # Happens after we have read the response headers from the backend.
    # See http://www.varnish-cache.org/docs/3.0/tutorial/vcl.html#vcl_fetch
}

sub vcl_deliver {
    # Happens when we have all the pieces we need, and are about to send the
    # response to the client.
    # See http://www.varnish-cache.org/docs/3.0/tutorial/vcl.html#vcl_fetch
}
