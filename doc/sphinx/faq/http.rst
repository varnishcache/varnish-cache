%%%%%%%%%%%
HTTP
%%%%%%%%%%%

**What is the purpose of the X-Varnish HTTP header?** 

The X-Varnish HTTP header allows you to find the correct log-entries for the transaction. For a cache hit, X-Varnish will contain both the ID of the current request and the ID of the request that populated the cache. It makes debugging Varnish a lot easier.

**Does Varnish support compression?**

This is a simple question with a complicated answer; see `WIKI <http://varnish-cache.org/wiki/FAQ/Compression>`_.

**How do I add a HTTP header?**

To add a HTTP header, unless you want to add something about the client/request, it is best done in vcl_fetch as this means it will only be processed every time the object is fetched::

        sub vcl_fetch {
          # Add a unique header containing the cache servers IP address:
          remove beresp.http.X-Varnish-IP;
          set    beresp.http.X-Varnish-IP = server.ip;
          # Another header:
          set    beresp.http.Foo = "bar";
        }

**How can I log the client IP address on the backend?**

All I see is the IP address of the varnish server.  How can I log the client IP address?

We will need to add the IP address to a header used for the backend request, and configure the backend to log the content of this header instead of the address of the connecting client (which is the varnish server).

Varnish configuration::

        sub vcl_recv {
          # Add a unique header containing the client address
          remove req.http.X-Forwarded-For;
          set    req.http.X-Forwarded-For = client.ip;
          # [...]
        }

For the apache configuration, we copy the "combined" log format to a new one we call "varnishcombined", for instance, and change the client IP field to use the content of the variable we set in the varnish configuration::

        LogFormat "%{X-Forwarded-For}i %l %u %t \"%r\" %>s %b \"%{Referer}i\" \"%{User-Agent}i\"" varnishcombined

And so, in our virtualhost, you need to specify this format instead of "combined" (or "common", or whatever else you use)::
        
	<VirtualHost *:80>
          ServerName www.example.com
          # [...]
          CustomLog /var/log/apache2/www.example.com/access.log varnishcombined
          # [...]
        </VirtualHost>

The [http://www.openinfo.co.uk/apache/index.html mod_extract_forwarded Apache module] might also be useful.


