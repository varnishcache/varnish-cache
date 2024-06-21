#-
# Copyright (c) 2006 Verdens Gang AS
# Copyright (c) 2006-2015 Varnish Software AS
# All rights reserved.
#
# Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# This is the builtin VCL code

vcl 4.0;

#######################################################################
# Client side

sub vcl_recv {
	call vcl_builtin_recv;
	return (hash);
}

sub vcl_builtin_recv {
	call vcl_req_host;
	call vcl_req_method;
	call vcl_req_authorization;
	call vcl_req_cookie;
}

sub vcl_req_host {
	if (req.http.host ~ "[[:upper:]]") {
		set req.http.host = req.http.host.lower();
	}
	if (!req.http.host &&
	    req.esi_level == 0 &&
	    req.proto == "HTTP/1.1") {
		# In HTTP/1.1, Host is required.
		return (synth(400));
	}
}

sub vcl_req_method {
	if (req.method == "PRI") {
		# This will never happen in properly formed traffic.
		return (synth(405));
	}
	if (req.method != "GET" &&
	    req.method != "HEAD" &&
	    req.method != "PUT" &&
	    req.method != "POST" &&
	    req.method != "TRACE" &&
	    req.method != "OPTIONS" &&
	    req.method != "DELETE" &&
	    req.method != "PATCH") {
		# Non-RFC2616 or CONNECT which is weird.
		return (pipe);
	}
	if (req.method != "GET" && req.method != "HEAD") {
		# We only deal with GET and HEAD by default.
		return (pass);
	}
}

sub vcl_req_authorization {
	if (req.http.Authorization) {
		# Not cacheable by default.
		return (pass);
	}
}

sub vcl_req_cookie {
	if (req.http.Cookie) {
		# Risky to cache by default.
		return (pass);
	}
}

sub vcl_pipe {
	call vcl_builtin_pipe;
	# By default "Connection: close" is set on all piped requests, to stop
	# connection reuse from sending future requests directly to the
	# (potentially) wrong backend. If you do want this to happen, you can
	# undo it here:
	# unset bereq.http.connection;
	return (pipe);
}

sub vcl_builtin_pipe {
}

sub vcl_connect {
    return (connect);
}

sub vcl_pass {
	call vcl_builtin_pass;
	return (fetch);
}

sub vcl_builtin_pass {
}

sub vcl_hash {
	call vcl_builtin_hash;
	return (lookup);
}

sub vcl_builtin_hash {
	hash_data(req.url);
	if (req.http.host) {
		hash_data(req.http.host);
	} else {
		hash_data(server.ip);
	}
}

sub vcl_purge {
	call vcl_builtin_purge;
	return (synth(200, "Purged"));
}

sub vcl_builtin_purge {
}

sub vcl_hit {
	call vcl_builtin_hit;
	return (deliver);
}

sub vcl_builtin_hit {
}

sub vcl_miss {
	call vcl_builtin_miss;
	return (fetch);
}

sub vcl_builtin_miss {
}

sub vcl_deliver {
	call vcl_builtin_deliver;
	return (deliver);
}

sub vcl_builtin_deliver {
}

#
# We can come here "invisibly" with the following errors: 500 & 503
#
sub vcl_synth {
	call vcl_builtin_synth;
	return (deliver);
}

sub vcl_builtin_synth {
	set resp.http.Content-Type = "text/html; charset=utf-8";
	set resp.http.Retry-After = "5";
	set resp.body = {"<!DOCTYPE html>
<html>
  <head>
    <title>"} + resp.status + " " + resp.reason + {"</title>
  </head>
  <body>
    <h1>Error "} + resp.status + " " + resp.reason + {"</h1>
    <p>"} + resp.reason + {"</p>
    <h3>Guru Meditation:</h3>
    <p>XID: "} + req.xid + {"</p>
    <hr>
    <p>Varnish cache server</p>
  </body>
</html>
"};
}

#######################################################################
# Backend Fetch

sub vcl_backend_fetch {
	call vcl_builtin_backend_fetch;
	return (fetch);
}

sub vcl_builtin_backend_fetch {
	if (bereq.method == "GET") {
		unset bereq.body;
	}
}

sub vcl_backend_response {
	call vcl_builtin_backend_response;
	return (deliver);
}

sub vcl_builtin_backend_response {
	call vcl_beresp_range;
	if (bereq.uncacheable) {
		return (deliver);
	}
	call vcl_beresp_stale;
	call vcl_beresp_cookie;
	call vcl_beresp_control;
	call vcl_beresp_vary;
}

sub vcl_beresp_stale {
	if (beresp.ttl <= 0s) {
		call vcl_beresp_hitmiss;
	}
}

sub vcl_beresp_cookie {
	if (beresp.http.Set-Cookie) {
		call vcl_beresp_hitmiss;
	}
}

sub vcl_beresp_control {
	if (beresp.http.Surrogate-control ~ "(?i)no-store" ||
	    (!beresp.http.Surrogate-Control &&
	      beresp.http.Cache-Control ~ "(?i:no-cache|no-store|private)")) {
		call vcl_beresp_hitmiss;
	}
}

sub vcl_beresp_vary {
	if (beresp.http.Vary == "*") {
		call vcl_beresp_hitmiss;
	}
}

sub vcl_beresp_range {
	if (beresp.status != 206 && beresp.status != 416) {
		# Content-Range has no meaning for these status codes
		# Ref: https://www.rfc-editor.org/rfc/rfc9110.html#section-14.4
		unset beresp.http.Content-Range;
	}
}

sub vcl_beresp_hitmiss {
	set beresp.ttl = 120s;
	set beresp.uncacheable = true;
	return (deliver);
}

sub vcl_backend_error {
	call vcl_builtin_backend_error;
	return (deliver);
}

sub vcl_builtin_backend_error {
	set beresp.http.Content-Type = "text/html; charset=utf-8";
	set beresp.http.Retry-After = "5";
	set beresp.body = {"<!DOCTYPE html>
<html>
  <head>
    <title>"} + beresp.status + " " + beresp.reason + {"</title>
  </head>
  <body>
    <h1>Error "} + beresp.status + " " + beresp.reason + {"</h1>
    <p>"} + beresp.reason + {"</p>
    <h3>Guru Meditation:</h3>
    <p>XID: "} + bereq.xid + {"</p>
    <hr>
    <p>Varnish cache server</p>
  </body>
</html>
"};
}

#######################################################################
# Housekeeping

sub vcl_init {
	return (ok);
}

sub vcl_fini {
	return (ok);
}
