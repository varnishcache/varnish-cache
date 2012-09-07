.. _tutorial-starting_varnish:

Starting Varnish
----------------

You might want to run ``pkill varnishd`` to make sure varnishd isn't
already running. Become root and type:

``# /usr/local/sbin/varnishd -f /usr/local/etc/varnish/default.vcl -s malloc,1G -a :80``

I added a few options, lets go through them:

``-f /usr/local/etc/varnish/default.vcl``
 The -f options specifies what configuration varnishd should use. If
 you are on a Linux system and have installed Varnish through packages
 the configuration files might reside in ``/etc/varnish``.

``-s malloc,1G``
 The -s options chooses the storage type Varnish should use for
 storing its content. I used the type *malloc*, which uses memory for
 storage. There are other backends as well, described in
 :ref:`user-guide-storage`. 1G specifies how much memory should be
 allocated - one gigabyte. 

Now you have Varnish running. Let us make sure that it works
properly. Use your browser to go to http://192.168.2.2/
(obviously, you should replace the IP address with one on your own
system) - you should now see your web application running there.

There are many command line options available for Varnish. For a walk
through the most important ones see :ref:`users-guide-command-line` or
for a complete list see :ref:`ref-varnishd`. 

Ignore that for the moment, we'll revisit that topic in the Users
Guide :ref:`users-guide-increasing_your_hitrate`.
