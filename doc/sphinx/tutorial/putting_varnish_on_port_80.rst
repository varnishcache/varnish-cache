
Put Varnish on port 80
----------------------

Until now we've been running with Varnish on a high port, for testing
purposes. You should test your application and if it works OK we can
switch, so Varnish will be running on port 80 and your web server on a
high port.

First we kill off varnishd.::

     # pkill varnishd

and stop your web server. Edit the configuration for your web server
and make it bind to port 8080 instead of 80. Now open the Varnish
default.vcl and change the port of the *default* backend to 8080.

Start up your web server and then start varnish.::

      # varnishd -f /usr/local/etc/varnish/default.vcl -s malloc,1G -T 127.0.0.1:2000

Note that we've removed the -a option. Now Varnish, as its default
setting dictates, will bind to the http port (80). Now everyone thats
accessing your site will be accessing through Varnish.

