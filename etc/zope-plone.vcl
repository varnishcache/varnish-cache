#
# This is an example VCL configuration file for varnish, meant for the
# Plone CMS running within Zope.  It defines a "default" backend for
# serving static content from a normal web server, and a "zope"
# backend for requests to the Zope CMS
#
# See the vcl(7) man page for details on VCL syntax and semantics.
#
# $Id$
#

# Default backend definition.  Set this to point to your content
# server.

# Default backend is the Zope CMS
backend default {
	set backend.host = "127.0.0.1";
	set backend.port = "9673";
}

acl purge {
	"localhost";
	"192.0.2.0"/24;
}

sub vcl_recv {

	# Normalize host headers, and do rewriting for the zope sites.  Reject
	# requests for unknown hosts
        if (req.http.host ~ "(www.)?example.com") {
                set req.http.host = "example.com";
                set req.url = "/VirtualHostBase/http/example.com:80/example.com/VirtualHostRoot" req.url;
        } elsif (req.http.host ~ "(www.)?example.org") {
                set req.http.host = "example.org";
                set req.url = "/VirtualHostBase/http/example.org:80/example.org/VirtualHostRoot" req.url;
        } else {
                error 404 "Unknown virtual host.";
        }

        # Handle special requests
        if (req.request != "GET" && req.request != "HEAD") {

                # POST - Logins and edits
                if (req.request == "POST") {
                        pass;
                }
                
                # PURGE - The CacheFu product can invalidate updated URLs
                if (req.request == "PURGE") {
                        if (!client.ip ~ purge) {
                                error 405 "Not allowed.";
                        }
                        lookup;
                }
        }

        # Don't cache authenticated requests
        if (req.http.Cookie && req.http.Cookie ~ "__ac(|_(name|password|persistent))=") {

		# Force lookup of specific urls unlikely to need protection
		if (req.url ~ "\.(js|css)") {
                        remove req.http.cookie;
                        lookup;
                }
                pass;
        }

        # The default vcl_recv is used from here.
 }

# Do the PURGE thing
sub vcl_hit {
        if (req.request == "PURGE") {
                set obj.ttl = 0s;
                error 200 "Purged";
        }
}
sub vcl_miss {
        if (req.request == "PURGE") {
                error 404 "Not in cache";
        }
}

# Enforce a minimum TTL, since we can PURGE changed objects actively
# from Zope by using the CacheFu product

sub vcl_fetch {
        if (obj.ttl < 3600s) {
                set obj.ttl = 3600s;
        }
}
