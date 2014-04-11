Hashing
-------

Internally, when Varnish stores content in the cache it stores the object
together with a hash key to find the object again. In the default setup
this key is calculated based on the content of the *Host* header or the
IP address of the server and the URL.

Behold the `default vcl`::

    sub vcl_hash {
        hash_data(req.url);
        if (req.http.host) {
            hash_data(req.http.host);
        } else {
            hash_data(server.ip);
        }
        return (lookup);
    }

As you can see it first checks in `req.url` then `req.http.host` if
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

As the default VCL will take care of adding the host and URL to the hash
we don't have to do anything else. Be careful calling ``return (lookup)``
as this will abort the execution of the default VCL and Varnish can end
up returning data based on more or less random inputs.
