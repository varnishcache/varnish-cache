#
# This is a basic VCL configuration file for varnish.  See the vcl(7)
# man page for details on VCL syntax and semantics.
#
# $Id$
#

# Default backend definition.  Set this to point to your content
# server.

backend default {
	set backend.host = "127.0.0.1";
	set backend.port = "8080";
}

# Below is a commented-out copy of the default VCL logic.  If you
# redefine any of these subroutines, the built-in logic will be
# appended to your code.

## Called when a client request is received
#
#sub vcl_recv {
#	if (req.request != "GET" && req.request != "HEAD") {
#		pipe;
#	}
#	if (req.http.Expect) {
#		pipe;
#	}
#	if (req.http.Authenticate || req.http.Cookie) {
#		pass;
#	}
#	lookup;
#}
#
## Called when entering pipe mode
#
#sub vcl_pipe {
#	pipe;
#}
#
## Called when entering pass mode
#
#sub vcl_pass {
#	pass;
#}
#
## Called when entering an object into the cache
#
#sub vcl_hash {
#	set req.hash += req.url;
#	set req.hash += req.http.host;
#	hash;
#}
#
## Called when the requested object was found in the cache
#
#sub vcl_hit {
#	if (!obj.cacheable) {
#		pass;
#	}
#	deliver;
#}
#
## Called when the requested object was not found in the cache
#
#sub vcl_miss {
#	fetch;
#}
#
## Called when the requested object has been retrieved from the
## backend, or the request to the backend has failed
#
#sub vcl_fetch {
#	if (!obj.valid) {
#		error;
#	}
#	if (!obj.cacheable) {
#		pass;
#	}
#	if (obj.http.Set-Cookie) {
#		pass;
#	}
#	insert;
#}
#
#
## Called before a cached object is delivered to the client
#
#sub vcl_deliver {
#    deliver;
#}
#
## Called when an object nears its expiry time
#
#sub vcl_timeout {
#	discard;
#}
#
## Called when an object is about to be discarded
#
#sub vcl_discard {
#    discard;
#}
