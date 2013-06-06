.. _users-guide-vary:

HTTP Vary
---------

_HTTP Vary is not a trivial concept. It is by far the most
misunderstood HTTP header._

The Vary header is sent by the web server to indicate what makes a
HTTP object Vary. This makes a lot of sense with headers like
Accept-Language. When a server issues a "Vary: Accept-Accept" it tells
Varnish that its needs to cache a separate version for every different
Accept-Language that is coming from the clients. 

So, if a client says it accepts the languages "en-us, en-uk" Varnish
will serve a different version to a client that says it accepts the
languages "da, de".

Please note that the headers that Vary refer to need to match
_exactly_ for there to be a match. So Varnish will keep two copies of
a page if one of them was created for "en-us, en-uk" and the other for
"en-us,en-uk". 

To achieve a high hitrate whilst using Vary is there therefor crucial
to normalize the headers the backends varies on. Remember, just a
differce in case can force different cache entries.


The following VCL code will normalize the Accept-Language headers, to
one of either "en","de" or "fr"::

    if (req.http.Accept-Language) {
        if (req.http.Accept-Language ~ "en") {
            set req.http.Accept-Language = "en";
        } elsif (req.http.Accept-Language ~ "de") {
            set req.http.Accept-Language = "de";
        } elsif (req.http.Accept-Language ~ "fr") {
            set req.http.Accept-Language = "fr";
        } else {
            # unknown language. Remove the accept-language header and 
	    # use the backend default.
            remove req.http.Accept-Language
        }
    }

The code sets the Accept-Encoding header from the client to either
gzip, deflate with a preference for gzip.

Vary parse errors
~~~~~~~~~~~~~~~~~

Varnish will return a 503 internal server error page when it fails to
parse the Vary server header, or if any of the client headers listed
in the Vary header exceeds the limit of 65k characters. An SLT_Error
log entry is added in these cases.

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

