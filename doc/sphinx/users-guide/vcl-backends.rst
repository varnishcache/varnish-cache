..
	Copyright (c) 2012-2020 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

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

We remove the comment markings in this code block making it look like.::

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

The "none" backend
------------------

Backends can also be declared as ``none`` with the following syntax:::

    backend default none;

``none`` backends are special:

* All backends declared ``none`` compare equal::

    backend a none;
    backend b none;

    sub vcl_recv {
        set req.backend_hint = a;
        if (req.backend_hint == b) {
            return (synth(200, "this is true"));
        }
    }

* The ``none`` backend evaluates to ``false`` when used in a boolean
  context::

    backend nil none;

    sub vcl_recv {
        set req.backend_hint = nil;
        if (! req.backend_hint) {
            return (synth(200, "We get here"));
        }
    }

* When directors find no healthy backend, they typically return the
  ``none`` backend

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

Without an explicit backend selection, Varnish will continue using
the `default` backend. If there is no backend named `default`, the
first backend found in the vcl will be used as the default backend.


Backends and virtual hosts in Varnish
-------------------------------------

Varnish fully supports virtual hosts. They might however work in a somewhat
counter-intuitive fashion since they are never declared
explicitly. You set up the routing of incoming HTTP requests in
`vcl_recv`. If you want this routing to be done on the basis of virtual
hosts you just need to inspect `req.http.host`.

You can have something like this::

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
more tight, maybe relying on the ``==`` operator instead, like this::

    sub vcl_recv {
        if (req.http.host == "foo.com" || req.http.host == "www.foo.com") {
            set req.backend_hint = foo;
        }
    }


Connecting Through a Proxy
--------------------------

.. _PROXY2: https://raw.githubusercontent.com/haproxy/haproxy/master/doc/proxy-protocol.txt
.. _haproxy: http://www.haproxy.org/
.. _SNI: https://en.wikipedia.org/wiki/Server_Name_Indication

As of this release, Varnish can connect to an actual *destination*
through a *proxy* using the `PROXY2`_ protocol. Other protocols may be
added.

For now, a typical use case of this feature is to make TLS-encrypted
connections through a TLS *onloader*. The *onloader* needs to support
dynamic connections with the destination address information taken
from a `PROXY2`_ preamble. For example with `haproxy`_ Version 2.2 or
higher, this snippet can be used as a basis for configuring an
*onloader*::

     # to review and adjust:
     # - maxconn
     # - bind ... mode ...
     # - ca-file ...
     #
     listen sslon
            mode    tcp
            maxconn 1000
            bind    /path/to/sslon accept-proxy mode 777
            stick-table type ip size 100
            stick   on dst
            server  s00 0.0.0.0:0 ssl ca-file /etc/ssl/certs/ca-bundle.crt alpn http/1.1 sni fc_pp_authority
            server  s01 0.0.0.0:0 ssl ca-file /etc/ssl/certs/ca-bundle.crt alpn http/1.1 sni fc_pp_authority
            server  s02 0.0.0.0:0 ssl ca-file /etc/ssl/certs/ca-bundle.crt alpn http/1.1 sni fc_pp_authority
            # ...
            # A higher number of servers improves TLS session caching

Varnish running on the same server/namespace can then use the
*onloader* with the ``.via`` feature (see :ref:`backend_definition_via`)::

  backend sslon {
    .path = "/path/to/sslon";
  }

  backend destination {
    .host = "my.https.service";
    .port = "443";
    .via = sslon;
  }

The ``.authority`` attribute can be used to specify the `SNI`_ for the
connection if it differs from ``.host``.


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
        .host = "192.168.0.11";
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
also a *random* director which distributes requests in a, you guessed it,
random fashion. If that is not enough, you can also write your own director
(see :ref:`ref-writing-a-director`).

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
:ref:`vcl(7)` documentation for more information.

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

Layering
--------

By default, most directors' ``.backend()`` methods return a reference
to the director itself. This allows for layering, like in this
example::

    import directors;

    sub vcl_init {
        new dc1 = directors.round_robin();
        dc1.add_backend(server1A);
        dc1.add_backend(server1B);

        new dc2 = directors.round_robin();
        dc2.add_backend(server2A);
        dc2.add_backend(server2B);

        new dcprio = directors.fallback();
        dcprio.add_backend(dc1);
        dcprio.add_backend(dc2);
    }

With this initialization, ``dcprio.backend()`` will resolve to either
``server1A`` or ``server1B`` if both are healthy or one of them if
only one is healthy. Only if both are sick will a healthy server from
``dc2`` be returned, if any.

Director Resolution
-------------------

The actual resolution happens when the backend connection is prepared
after a return from ``vcl_backend_fetch {}`` or ``vcl_pipe {}``.

In some cases like server sharding the resolution outcome is required
already in VCL. For such cases, the ``.resolve()`` method can be used,
like in this example::

	set req.backend_hint = dcprio.backend().resolve();

When using this statement with the previous example code,
``req.backend_hint`` will be set to one of the ``server*`` backends or
the ``none`` backend if they were all sick.

``.resolve()`` works on any object of the ``BACKEND`` type.


.. _users-guide-advanced_backend_connection-pooling:

Connection Pooling
------------------

Opening connections to backends always comes at a cost: Depending on
the type of connection and backend infrastructure, the overhead for
opening a new connection ranges from pretty low for a local Unix
domain socket (see :ref:`backend_definition` ``.path`` attribute) to
substantial for establishing possibly multiple TCP and/or TLS
connections over possibly multiple hops and long network
paths. However relevant the overhead, it certainly always exists.

So because re-using existing connections can generally be considered
to reduce overhead and latencies, Varnish pools backend connections by
default: Whenever a backend task is finished, the used connection is
not closed but rather added to a pool for later reuse. To avoid a
connection from being reused, the ``Connection: close`` http header
can be added in :ref:`vcl_backend_fetch`.

While backends are defined per VCL, connection pooling works across
VCLs and even across backends: By default, the identifier for pooled
connections is constructed from the ``.host``\ /\ ``.port`` or
``.path`` attributes of the :ref:`backend_definition` (VMODs can make
use of custom identifiers). So whenever two backends share the same
address information, irrespective of which VCLs they are defined in,
their connections are taken from a common pool.

If not actively closed by the backend, pooled connections are kept
open by Varnish until the :ref:`ref_param_backend_idle_timeout`
expires.
