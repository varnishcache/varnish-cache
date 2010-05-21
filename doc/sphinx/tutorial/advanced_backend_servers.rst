Advanced Backend configuration
------------------------------

At some point you might need Varnish to cache content from several
servers. You might want Varnish to map all the URL into one single
host or not. There are lot of options.

Lets say we need to introduce a Java application into out PHP web
site. Lets say our Java application should handle URL beginning with
/java/.

We manage to get the thing up and running on port 8000. Now, lets have
a look a default.vcl.::

  backend default {
      .host = "127.0.0.1";
      .port = "8080";
  }

We add a new backend.::

  backend java {
      .host = "127.0.0.1";
      .port = "8000";
  }

Now we need tell where to send the difference URL. Lets look at vcl_recv.::

  sub vcl_recv {
      if (req.url ~ "^/java/") {
          set req.backend = java;
      } else {
          set req.backend = default.
      }
  }

It's quite simple, really. Lets stop and think about this for a
moment. As you can see you can define how you choose backends based on
really arbitrary data. You want to send mobile devices to a different
backend? No problem. if (req.User-agent ~ /mobile/) .... should do the
trick. 

Directors
---------

You can also group several backend into a group of backends. These
groups are called directors. This will give you increased performance
and resillience. You can define several backends and group them
together in a director.::

	 backend server1 {
	     .host = "192.168.0.10";
	 }
	 backend server2{
	     .host = "192.168.0.10";
	 }

Now we create the director.::

       	director example_director round-robin {
        {
                .backend = server1;
        }
	# server2
        {
                .backend = server2;
        }
	# foo
	}


This director is a round-robin director. This means the director will
distribute the incomming requests on a round-robin basis. There is
also a *random* director which distributes requests in a, you guessed
it, random fashion.

But what if one of your servers goes down? Can Varnish direct all the
requests to the healthy server? Sure it can. This is where the Health
Checks come into play.

.. _tutorial-advanced_backend_servers-health:

Health checks
-------------

Lets set up a director with two backends and health checks. First lets
define the backends.::

       backend server1 {
         .host = "server1.example.com";
	 .probe = {
                .url = "/";
                .interval = 5s;
                .timeout = 1 s;
                .window = 5;
                .threshold = 3;
	   }
         }
       backend server2 {
  	  .host = "server2.example.com";
  	  .probe = {
                .url = "/";
                .interval = 5s;
                .timeout = 1 s;
                .window = 5;
                .threshold = 3;
	  }
        }

Whats new here is the probe. Varnish will check the health of each
backend with a probe. The options are

url
 What URL should varnish request.

interval
 How often should we poll

timeout
 What is the timeout of the probe

window
 Varnish will maintain a *sliding window* of the results. Here the
 window has five checks.

threshold 
 How many of the .window last polls must be good for the backend to be declared healthy.

Now we define the director.::

  director example_director round-robin {
        {
                .backend = server1;
        }
        # server2 
        {
                .backend = server2;
        }
	
        }

You use this director just as you would use any other director or
backend. Varnish will not send traffic to hosts that are marked as
unhealty. Varnish can also serve stale content if all the backends are
down. See :ref:`tutorial-handling_misbehaving_servers` for more
information on how to enable this.
