.. _users-guide-backend_servers:

Backend servers
---------------

Varnish has a concept of "backend" or "origin" servers. A backend
server is the server providing the content Varnish will accelerate.

Our first task is to tell Varnish where it can find its backends. Start
your favorite text editor and open the relevant VCL file.

Somewhere in the top there will be a section that looks a bit like this.::

    # backend default {
    #     .host = "127.0.0.1";
    #     .port = "8080";
    # }

We remove the comment markings in this text stanza making the it look like.::

    backend default {
        .host = "127.0.0.1";
        .port = "8080";
    }

Now, this piece of configuration defines a backend in Varnish called
*default*. When Varnish needs to get content from this backend it will
connect to port 8080 on localhost (127.0.0.1).

Varnish can have several backends defined you can even join
several backends together into clusters of backends for load balancing
purposes.


Multiple backends
-----------------

At some point you might need Varnish to cache content from several
servers. You might want Varnish to map all the URL into one single
host or not. There are lot of options.

Lets say we need to introduce a Java application into out PHP web
site. Lets say our Java application should handle URL beginning with
`/java/`.

We manage to get the thing up and running on port 8000. Now, lets have
a look at the `default.vcl`.::

    backend default {
        .host = "127.0.0.1";
        .port = "8080";
    }

We add a new backend.::

    backend java {
        .host = "127.0.0.1";
        .port = "8000";
    }

Now we need tell Varnish where to send the difference URL. Lets look at `vcl_recv`.::

    sub vcl_recv {
        if (req.url ~ "^/java/") {
            set req.backend_hint = java;
        } else {
            set req.backend_hint = default;
        }
    }

It's quite simple, really. Lets stop and think about this for a
moment. As you can see you can define how you choose backends based on
really arbitrary data. You want to send mobile devices to a different
backend? No problem. ``if (req.http.User-agent ~ /mobile/) ..`` should do the
trick.


Backends and virtual hosts in Varnish
-------------------------------------

Varnish fully supports virtual hosts. They might however work in a somewhat
counter-intuitive fashion since they are never declared
explicitly. You set up the routing of incoming HTTP requests in
`vcl_recv`. If you want this routing to be done on the basis of virtual
hosts you just need to inspect `req.http.host`.

You can have something like this:::

    sub vcl_recv {
        if (req.http.host ~ "foo.com") {
            set req.backend_hint = foo;
        } elsif (req.http.host ~ "bar.com") {
            set req.backend_hint = bar;
        }
    }

Note that the first regular expressions will match "foo.com",
"www.foo.com", "zoop.foo.com" and any other host ending in "foo.com". In
this example this is intentional but you might want it to be a bit
more tight, maybe relying on the ``==`` operator in stead, like this:::

    sub vcl_recv {
        if (req.http.host == "foo.com" || req.http.host == "www.foo.com") {
            set req.backend_hint = foo;
        }
    }


.. _users-guide-advanced_backend_servers-directors:


Directors
---------

You can also group several backend into a group of backends. These
groups are called directors. This will give you increased performance
and resilience.

You can define several backends and group them together in a
director. This requires you to load a VMOD, a Varnish module, and then to
call certain actions in `vcl_init`.::


    import directors;    # load the directors

    backend server1 {
        .host = "192.168.0.10";
    }
    backend server2 {
        .host = "192.168.0.10";
    }

    sub vcl_init {
        new bar = directors.round_robin();
        bar.add_backend(server1);
        bar.add_backend(server2);
    }

    sub vcl_recv {
        # send all traffic to the bar director:
        set req.backend_hint = bar.backend();
    }

This director is a round-robin director. This means the director will
distribute the incoming requests on a round-robin basis. There is
also a *random* director which distributes requests in a, you guessed
it, random fashion.

But what if one of your servers goes down? Can Varnish direct all the
requests to the healthy server? Sure it can. This is where the Health
Checks come into play.

.. _users-guide-advanced_backend_servers-health:

Health checks
-------------

Lets set up a director with two backends and health checks. First let
us define the backends::

    backend server1 {
        .host = "server1.example.com";
        .probe = {
            .url = "/";
            .timeout = 1s;
            .interval = 5s;
            .window = 5;
            .threshold = 3;
        }
    }

    backend server2 {
        .host = "server2.example.com";
        .probe = {
            .url = "/";
            .timeout = 1s;
            .interval = 5s;
            .window = 5;
            .threshold = 3;
        }
    }

What is new here is the ``probe``.  In this example Varnish will check the
health of each backend every 5 seconds, timing out after 1 second. Each
poll will send a GET request to /. If 3 out of the last 5 polls succeeded
the backend is considered healthy, otherwise it will be marked as sick.

Refer to the :ref:`reference-vcl_probes` section in the
:ref:`reference-vcl` documentation for more information.

Now we define the 'director'::

    import directors;

    sub vcl_init {
        new vdir = directors.round_robin();
        vdir.add_backend(server1);
        vdir.add_backend(server2);
    }

You use this `vdir` director as a backend_hint for requests, just like
you would with a simple backend. Varnish will not send traffic to hosts
that are marked as unhealthy.

Varnish can also serve stale content if all the backends are down. See
:ref:`users-guide-handling_misbehaving_servers` for more information on
how to enable this.

Please note that Varnish will keep health probes running for all loaded
VCLs. Varnish will coalesce probes that seem identical - so be careful
not to change the probe config if you do a lot of VCL loading. Unloading
the VCL will discard the probes. For more information on how to do this
please see ref:`reference-vcl-director`.
