.. _tutorial-backend_servers:

Backend servers
---------------

Varnish has a concept of "backend" or "origin" servers. A backend
server is the server providing the content Varnish will accelerate.

Our first task is to tell Varnish where it can find its content. Start
your favorite text editor and open the varnish default configuration
file. If you installed from source this is
/usr/local/etc/varnish/default.vcl, if you installed from a package it
is probably /etc/varnish/default.vcl.

Somewhere in the top there will be a section that looks a bit like this.::

	  # backend default {
	  #     .host = "127.0.0.1";
	  #     .port = "8080";
	  # }

We comment in this bit of text and change the port setting from 8080
to 80, making the text look like.::

          backend default {
                .host = "127.0.0.1";
    		.port = "80";
	  }

Now, this piece of configuration defines a backend in Varnish called
*default*. When Varnish needs to get content from this backend it will
connect to port 80 on localhost (127.0.0.1).

Varnish can have several backends defined and can you can even join
several backends together into clusters of backends for load balancing
purposes. 

Now that we have the basic Varnish configuration done, let us start up
Varnish on port 8080 so we can do some fundamental testing on it.
