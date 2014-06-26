.. _users-guide-command-line:

Important command line arguments
--------------------------------

There a two command line arguments you have to set when starting Varnish, these are: 
* what TCP port to serve HTTP from, and 
* where the backend server can be contacted.

If you have installed Varnish through using a provided operating system bound package,
you will find the startup options here:

* Debian, Ubuntu: `/etc/default/varnish`
* Red Hat, Centos: `/etc/sysconfig/varnish`
* FreeBSD: `/etc/rc.conf` (See also: /usr/local/etc/rc.d/varnishd)


'-a' *listen_address*
^^^^^^^^^^^^^^^^^^^^^

The '-a' argument defines what address Varnish should listen to, and service HTTP requests from.

You will most likely want to set this to ":80" which is the Well
Known Port for HTTP.

You can specify multiple addresses separated by a comma, and you
can use numeric or host/service names if you like, Varnish will try
to open and service as many of them as possible, but if none of them
can be opened, `varnishd` will not start.

Here are some examples::

	-a :80
	-a localhost:80
	-a 192.168.1.100:8080
	-a '[fe80::1]:80'
	-a '0.0.0.0:8080,[::]:8081'

.. XXX:brief explanation of some of the more complex examples perhaps? benc

If your webserver runs on the same machine, you will have to move
it to another port number first.

'-f' *VCL-file* or '-b' *backend*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^


Varnish needs to know where to find the HTTP server it is caching for.
You can either specify it with the '-b' argument, or you can put it in your own VCL file, specified with the '-f' argument.

Using '-b' is a quick way to get started::

	-b localhost:81
	-b thatotherserver.example.com:80
	-b 192.168.1.2:80

Notice that if you specify a name, it can at most resolve to one IPv4
*and* one IPv6 address.

If you go with '-f', you can start with a VCL file containing just::

	backend default {
		.host = "localhost:81";
	}

which is exactly what '-b' does.

.. XXX:What happens if I start with -b and then have the backend defined in my VCL? benc

In both cases the built-in VCL code is appended.

Other options
^^^^^^^^^^^^^

Varnish comes with an abundance of useful command line arguments. We recommend that you study them but not necessary use them all, but to get started, the above will be sufficient.

By default Varnish will use 100 megabytes of malloc(3) storage
for caching objects, if you want to cache more than that, you
should look at the '-s' argument.

.. XXX: 3? benc

If you run a really big site, you may want to tune the number of
worker threads and other parameters with the '-p' argument,
but we generally advice not to do that unless you need to.

Before you go into production, you may also want to revisit the
chapter
:ref:`run_security` to see if you need to partition administrative
privileges.

For a complete list of the command line parameters please see
:ref:`ref-varnishd-options`.
