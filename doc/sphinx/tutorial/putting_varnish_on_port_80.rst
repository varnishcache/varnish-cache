
Put Varnish on port 80
----------------------

Until now we've been running with Varnish on a high port which is great for
testing purposes. Let's now put Varnish on the default HTTP port 80.

First we stop varnish: ``service varnish stop``

Now we need to edit the configuration file that starts Varnish.

Debian/Ubuntu
~~~~~~~~~~~~~

On Debian/Ubuntu this is `/etc/default/varnish`. In the file you'll find
some text that looks like this::

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

Red Hat Enterprise Linux / CentOS
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

On Red Hat/CentOS you can find a similar configuration file in
`/etc/sysconfig/varnish`.


Restarting Varnish again
------------------------

Once the change is done, restart Varnish: ``service varnish start``.

Now everyone accessing your site will be accessing through Varnish.

