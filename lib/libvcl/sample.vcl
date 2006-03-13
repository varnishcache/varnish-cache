
acl rfc1918 {
	10.0.0.0/8;
	172.16.0.0/12;
	192.168.0.0/16;
}

sub request_policy {

	if (client.ip == 10.1.2.3) {
		no_cache;
		finish;
	}

	if (client.ip ~ rfc1918) {
		no_cache;
		finish;
	}

	if (req.url.host ~ "cnn.no$") {
		rewrite "cnn.no$" "vg.no";
	}

	if (req.url.path ~ "cgi-bin") {
		no_cache;
	}

	if (req.useragent ~ "spider") {
		no_new_cache;
	}

# comment

	if (backend.response_time <= 0.8s) {
		set req.ttlfactor = 1.5;
	} elseif (backend.response_time > 1.5s) {
		set req.ttlfactor = 2.0;
	} elseif (backend.response_time > 2.5m) {
		set req.ttlfactor = 5.0;
	}

	/*
	 * the program contains no references to
	 * maxage, s-maxage and expires, so the
	 * default handling (RFC2616) applies
	 */
}

backend vg {
	set backend.ip = 10.0.0.100;
	set backend.timeout = 4s;
	set backend.bandwidth = 2000Mb/s;
}

backend chat {
	set backend.ip = 10.0.0.4;
	set backend.timeout = 4s;
	set backend.bandwidth = 2000Mb/s;
}

sub bail {
	error 404 "Bailing out";
	finish;
}

sub fetch_policy {

	if (!req.url.host ~ "/vg.no$/") {
		set req.backend = vg;
	} else {
		/* XXX: specify 404 page url ? */
		error 404;
	}

	if (backend.response_time > 2.0s) {
		if (req.url.path ~ "/landbrugspriser/") {
			call bail;
		}
	}
	fetch;
	if (backend.down) {
		if (obj.exist) {
			set obj.ttl += 10m;
			finish;
		}
		switch_config ohhshit;
	}
	if (obj.result == 404) {
		error 300 "http://www.vg.no";
	}
	if (obj.result != 200) {
		finish;
	}
	if (obj.size > 256kb) {
		no_cache;
	} else if (obj.size > 32kb && obj.ttl < 2m) {
		set obj.ttl = 5m;
	}
	if (backend.response_time > 2.0s) {
		set obj.ttl *= 2.0;
	}
}

sub prefetch_policy {

	if (obj.usage < 10 && obj.ttl < 5m) {
		fetch;
	}
}
