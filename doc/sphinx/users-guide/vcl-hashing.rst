..
	Copyright (c) 2012-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

Hashing
-------

Internally, when Varnish stores content in the cache indexed by a hash
key used to find the object again. In the default setup
this key is calculated based on `URL`, the `Host:` header, or
if there is none, the IP address of the server::

    sub vcl_hash {
        hash_data(req.url);
        if (req.http.host) {
            hash_data(req.http.host);
        } else {
            hash_data(server.ip);
        }
        return (lookup);
    }

As you can see it first hashes `req.url` and then `req.http.host` if
it exists. It is worth pointing out that Varnish doesn't lowercase the
hostname or the URL before hashing it so in theory having "Varnish.org/"
and "varnish.org/" would result in different cache entries. Browsers
however, tend to lowercase hostnames.

You can change what goes into the hash. This way you can make Varnish
serve up different content to different clients based on arbitrary
criteria.

Let's say you want to serve pages in different languages to your users
based on where their IP address is located. You would need some Vmod to
get a country code and then put it into the hash. It might look like this.

In `vcl_recv`::

    set req.http.X-Country-Code = geoip.lookup(client.ip);

And then add a `vcl_hash`::

    sub vcl_hash {
        hash_data(req.http.X-Country-Code);
    }

Because there is no `return(lookup)`, the builtin VCL will take care
of adding the URL, `Host:` or server IP# to the hash as usual.

If `vcl_hash` did return, ie::

    sub vcl_hash {
        hash_data(req.http.X-Country-Code);
        return(lookup);
    }

then *only* the country-code would matter, and Varnish would return
seemingly random objects, ignoring the URL, (but they would always
have the correct `X-Country-Code`).
