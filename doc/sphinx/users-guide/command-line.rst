.. _users-guide-command-line:

Typical command line options
----------------------------

If you run Varnish out of a package for your operating system,
you will find the default options here:

* Debian, Ubuntu: /etc/default/varnish
* Red Hat, Centos: /etc/sysconfig/varnish
* FreeBSD: /etc/rc.conf (See also: /usr/local/etc/rc.d/varnishd)

There some command line options you will simply have choose values for:

-a *listen_address*
^^^^^^^^^^^^^^^^^^^

What address should Varnish listen to and service HTTP requests on.

You will most likely want to set this to ":80" which is the Well
Known Port for HTTP.

You can specify multiple addresses separated by a comma, and you
can use numeric or host/service names as you like, varnish will try
to open and service as many of them as possible, but if none of them
can be opened, varnishd will not start.

Here are some examples::

	-a :80
	-a localhost:80
	-a 192.168.1.100:8080
	-a '[fe80::1]:80'
	-a '0.0.0.0:8080,[::]:8081'

If your webserver runs on the same computer, you will have to move
it to another port number first.
 
-f *VCL-file* or -b *backend*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Varnish needs to know where to find the HTTP server it is caching for.
You can either specify it with -b and use the default VCL code, or you
can put it in your own VCL file.

Using -b is a quick way to get started::

	-b localhost:81
	-b thatotherserver.example.com:80
	-b 192.168.1.2:80

Notice that if you specify a name, it can at most resolve to one IPv4
*and* one IPv6 address.

If you go with -f, you can start with a VCL file containing just::

	backend default {
		.host = "localhost:81";
	}

which is exactly what -b does.

-s *storage-options*
^^^^^^^^^^^^^^^^^^^^

This is probably the most important one. The default is to use
the memory storage backend and to allocate a small amount of
memory. On a small site this might suffice. If you have dedicated
Varnish Cache server you most definitivly want to increase
the memory allocated or consider another backend. 
Please note that in addition to the memory allocated by the
storage engine itself Varnish also has internal data structures
that consume memory. More or less 1kb per object.  
See also :ref:`guide-storage`.

-T *CLI-listen-address*  
^^^^^^^^^^^^^^^^^^^^^^^

Varnish has a built-in text-based administration
interface. Activating the interface makes Varnish manageble
without stopping it. You can specify what interface the
management interface should listen to. Make sure you don't expose
the management interface to the world as you can easily gain root
access to a system via the Varnish management interface. I
recommend tieing it to localhost. If you have users on your
system that you don't fully trust, use firewall rules to restrict
access to the interface to root only.

-S *CLI-secret-file*
^^^^^^^^^^^^^^^^^^^^

This file stores a secret you must know, in order to get
access to the CLI.

For a complete list of the command line parameters please see
:ref:`ref-varnishd-options`.

