Troubleshooting Varnish
-----------------------


When Varnish won't start
~~~~~~~~~~~~~~~~~~~~~~~~

Sometimes Varnish wont start. There is a pletphora of reasons why
Varnish wont start on your machine. We've seen everything from wrong
permissions on /dev/null to other processses blocking the ports.

Starting Varnish in debug mode to see what is going on.

Try to start varnish by::

    # varnishd -f /usr/local/etc/varnish/default.vcl -s malloc,1G -T 127.0.0.1:2000  -a 0.0.0.0:8080 -d

Notice the -d option. It will give you some more information on what
is going on. Let us see how Varnish will react to something else
listening on its port.::

    # varnishd -n foo -f /usr/local/etc/varnish/default.vcl -s malloc,1G -T 127.0.0.1:2000  -a 0.0.0.0:8080 -d
    storage_malloc: max size 1024 MB.
    Using old SHMFILE
    Platform: Linux,2.6.32-21-generic,i686,-smalloc,-hcritbit
    200 193     
    -----------------------------
    Varnish HTTP accelerator CLI.
    -----------------------------
    Type 'help' for command list.
    Type 'quit' to close CLI session.
    Type 'start' to launch worker process.

Now Varnish is running. Only the master process is running, in debug
mode the cache does not start. Now you're on the console. You can
instruct the master process to start the cache by issuing "start".::

	 start
	 bind(): Address already in use
	 300 22      
	 Could not open sockets

And here we have our problem. Something else is bound to the HTTP port
of Varnish. If this doesn't help try strace or truss or come find us
on IRC.


Varnish is crashing
~~~~~~~~~~~~~~~~~~~

When varnish goes bust.


Varnish doesn't cache
~~~~~~~~~~~~~~~~~~~~~

See :ref:`_tutorial-increasing_your_hitrate:`.

