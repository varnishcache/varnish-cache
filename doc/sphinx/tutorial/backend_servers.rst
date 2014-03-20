.. _tutorial-backend_servers:

Backend servers
---------------

Varnish has a concept of `backend` or origin servers. A backend
server is the server providing the content Varnish will accelerate via the cache.

Our first task is to tell Varnish where it can find its content. Start
your favorite text editor and open the Varnish default configuration
file. If you installed from source this is
`/usr/local/etc/varnish/default.vcl`, if you installed from a package it
is probably `/etc/varnish/default.vcl`.

If you've been following the tutorial there is probably a section of
the configuration that looks like this:::

  vcl 4.0;
  
  backend default {
      .host = "www.varnish-cache.org";
      .port = "80";
  }

This means we set up a backend in Varnish that fetches content from
the host www.varnish-cache.org on port 80.

Since you probably don't want to be mirroring varnish-cache.org we
need to get Varnish to fetch content from your own origin
server. We've already bound Varnish to the public port 80 on the
server so now we need to tie it to the origin.

For this example, let's pretend the origin server is running on
localhost, port 8080.::

  vcl 4.0;

  backend default {
    .host = "127.0.0.1";
    .port = "8080";
  }


Varnish can have several backends defined and can even join several backends
together into clusters of backends for load balancing purposes, having Varnish
pick one backend based on different algorithms. 

Next, let's have a look at some of what makes Varnish unique and what you can do with it.


