.. _tutorial-starting_varnish:

Starting Varnish
----------------

I assume varnishd is in your path. You might want to run ``pkill
varnishd`` to make sure Varnish isn't running. Become root and type:

``# varnishd -f /usr/local/etc/varnish/default.vcl -s malloc,1G -T 127.0.0.1:2000 -a 0.0.0.0:8080``

I added a few options, lets go through them:

``-f /usr/local/etc/varnish/default.vcl``
 The -f options specifies what configuration varnishd should use.

``-s malloc,1G``
 The -s options chooses the storage type Varnish should use for
 storing its content. I used the type *malloc*, which just uses memory
 for storage. There are other backends as well, described in 
 :ref:tutorial-storage. 1G specifies how much memory should be allocated 
 - one gigabyte.

``-T 127.0.0.1:2000``
 Varnish has a buildt in text-based administration
 interface. Activating the interface makes Varnish manageble without
 stopping it. You can specify what interface the management interface
 should listen to. Make sure you don't expose the management interface
 to the world as you can easily gain root access to a system via the
 Varnish management interace. I recommend tieing it to localhost. If
 you have users on your system that you don't fully trust use firewall
 rules to restrict access to the interace to root only.

``-a 0.0.0.0:8080``
 I specify that I want Varnish to listen on port 8080 for incomming
 HTTP requests. For a production environment you would probably make
 Varnish listen on port 80, which is the default.

Now you have Varnish running. Let us make sure that it works
properly. Use your browser to go to http://192.168.2.2:8080/ - you
should now see your web application running there.

Whether or not the application actually goes faster when run through
Varnish depends on a few factors. If you application uses cookies for
every session (a lot of PHP and Java applications seem to send a
session cookie if it is needed or not) or if it uses authentication
chances are Varnish won't do much caching. Ignore that for the moment,
we come back to that in :ref:`tutorial-increasing_your_hitrate`.

Lets make sure that Varnish really does do something to your web
site. To do that we'll take a look at the logs.
