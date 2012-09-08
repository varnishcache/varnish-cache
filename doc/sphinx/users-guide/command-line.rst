.. _users-guide-command-line:

XXX: Total rewrite of this

Command Line options
--------------------

I assume varnishd is in your path. You might want to run ``pkill
varnishd`` to make sure varnishd isn't running. 

Become root and type:

``# varnishd -f /usr/local/etc/varnish/default.vcl -s malloc,1G -T 127.0.0.1:2000 -a 0.0.0.0:8080``

I added a few options, lets go through them:

``-f /usr/local/etc/varnish/default.vcl``
 The -f options specifies what configuration varnishd should use.

``-s malloc,1G``
 The -s options chooses the storage type Varnish should use for
 storing its content. I used the type *malloc*, which just uses memory
 for storage. There are other backends as well, described in 
 :ref:users-guide-storage. 1G specifies how much memory should be allocated 
 - one gigabyte. 

``-T 127.0.0.1:2000``
 Varnish has a built-in text-based administration
 interface. Activating the interface makes Varnish manageble without
 stopping it. You can specify what interface the management interface
 should listen to. Make sure you don't expose the management interface
 to the world as you can easily gain root access to a system via the
 Varnish management interface. I recommend tieing it to localhost. If
 you have users on your system that you don't fully trust, use firewall
 rules to restrict access to the interface to root only.

``-a 0.0.0.0:8080``
 I specify that I want Varnish to listen on port 8080 for incomming
 HTTP requests. For a production environment you would probably make
 Varnish listen on port 80, which is the default.

For a complete list of the command line parameters please see
:ref:`ref-varnishd-options`.

