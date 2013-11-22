varnishtest "req.backend.healthy in vcl_deliver"

server s1 {
	rxreq
	txresp
} -start

varnish v1 -vcl+backend {

	import ${vmod_std};

	sub vcl_deliver {
	    set resp.http.x-foo = std.healthy(req.backend);
	}
} -start

client c1 {
	txreq
	rxresp
	expect resp.status == 200
} -run
