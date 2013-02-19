
Put Varnish on port 80
----------------------

Until now we've been running with Varnish on a high port, for testing
purposes. You probably don't want to do this in the long term so let's
put Varnish on port 80.

First we stop varnish::

     # service varnish stop

Now we need to edit the configuration file that starts Varnish. 


Debian/Ubuntu
~~~~~~~~~~~~~

On Debian/Ubuntu this is /etc/default/varnish. In the file you'll find some text that looks like this::

  DAEMON_OPTS="-a :6081 \
               -T localhost:6082 \
               -f /etc/varnish/default.vcl \
               -S /etc/varnish/secret \
               -s malloc,256m"

Change it to::

  DAEMON_OPTS="-a :80 \
               -T localhost:6082 \
               -f /etc/varnish/default.vcl \
               -S /etc/varnish/secret \
               -s malloc,256m"

Red Hat EL / Centos
~~~~~~~~~~~~~~~~~~~

On Red Hat EL / Centos
On Red Hat/Centos it is


Restarting Varnish
------------------

Once the change is done. Restart varnish: ``service varnish
restart``. Now everyone accessing your site will be accessing through
Varnish.

