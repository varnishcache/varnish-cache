..
	Copyright (c) 2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

Ignoring the Vary header for bots
=================================

Varnish supports HTTP variants out of the box, but the *Vary* header is
somewhat limited since it operates on complete header values. If you want for
example to conduct an A/B testing campaign or perform blue/green deployment
you can make clients "remember" their path with a first-party cookie.

When a search engine bot asks for contents however, there's a high chance that
they don't process cookies and in all likelihood you would prefer to serve a
response quickly. In that case you would probably prefer not to even try to
attribute a category to the client, but in that case you create a new variant
in your cache that is none of A, B, blue, green, or whatever your backend
serves.

If the way content is served makes no difference to the bot, because you
changed the color of a button or something else orthogonal to the content
itself, then you risk a cache miss with the detrimental effects of adding a
needless variant to the cache and serving it with extra latency.

If latency is paramount, you can use ``req.hash_ignore_vary`` to opt out of
the Vary match during the lookup and get the freshest variant.

Ignoring how the cookie is set, and assuming the backend always provides an
accurate *Cache-Control* even when cookies are present, below is an example of
an A/B testing setup where bots are served the freshest variant::


    import cookie;

    include "devicedetect.vcl";

    sub vcl_recv {
        call devicedetect;
        if (req.http.X-UA-Device ~ "bot") {
            set req.hash_ignore_vary = true;
        }
    }

    sub vcl_req_cookie {
        cookie.parse(req.http.Cookie);
        set req.http.X-AB-Test = cookie.get("ab-test");
        return;
    }

    sub vcl_deliver {
        unset resp.http.Vary;
    }

It is also assumed that the backend replies with a ``Vary: X-AB-Test`` header
and varies on no other header.
