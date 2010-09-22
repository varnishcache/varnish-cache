
Put Varnish on port 80
----------------------

If your application works OK we can now switch the ports so Varnish
will listen to port 80. Kill varnish.::

     # pkill varnishd

and stop your web server. Edit the configuration for your web server
and make it bind to port 8080 instead of 80. Now open the Varnish
default.vcl and change the port of the default backend to 8080.

Start up your web server and then start varnish.::

      # varnishd -f /usr/local/etc/varnish/default.vcl -s malloc,1G -T 127.0.0.1:2000

We're removed the -a option. Now Varnish will bind to the http port as
it is its default. Now try your web application and see if it works
OK.
