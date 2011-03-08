Troubleshooting Varnish
-----------------------

Sometimes Varnish misbehaves. In order for you to understand whats
going on there are a couple of places you can check. varnishlog,
/var/log/syslog, /var/log/messages are all places where varnish might
leave clues of whats going on.


When Varnish won't start
~~~~~~~~~~~~~~~~~~~~~~~~

Sometimes Varnish wont start. There is a plethora of reasons why
Varnish wont start on your machine. We've seen everything from wrong
permissions on /dev/null to other processes blocking the ports.

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
    Varnish Cache CLI.
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

When varnish goes bust the child processes crashes. Usually the mother
process will manage this by restarting the child process again. Any
errors will be logged in syslog. It might look like this:::

       Mar  8 13:23:38 smoke varnishd[15670]: Child (15671) not responding to CLI, killing it.
       Mar  8 13:23:43 smoke varnishd[15670]: last message repeated 2 times
       Mar  8 13:23:43 smoke varnishd[15670]: Child (15671) died signal=3
       Mar  8 13:23:43 smoke varnishd[15670]: Child cleanup complete
       Mar  8 13:23:43 smoke varnishd[15670]: child (15697) Started

Specifically if you see the "Error in munmap" error on Linux you might
want to increase the amount of maps available. Linux is limited to a
maximum of 64k maps. Setting vm.max_max_count i sysctl.conf will
enable you to increase this limit. You can inspect the number of maps
your program is consuming by counting the lines in /proc/$PID/maps

This is a rather odd thing to document here - but hopefully Google
will serve you this page if you ever encounter this error. 

Varnish gives me Guru meditation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

First find the relevant log entries in varnishlog. That will probably
give you a clue. Since varnishlog logs so much data it might be hard
to track the entries down. You can set varnishlog to log all your 503
errors by issuing the following command:::

   $ varnishlog -c -o TxStatus 503

If the error happened just a short time ago the transaction might still
be in the shared memory log segment. To get varnishlog to process the
whole shared memory log just add the -d option:::

   $ varnishlog -d -c -o TxStatus 503

Please see the varnishlog man page for elaborations on further
filtering capabilities and explanation of the various options.


Varnish doesn't cache
~~~~~~~~~~~~~~~~~~~~~

See :ref:`tutorial-increasing_your_hitrate`.

