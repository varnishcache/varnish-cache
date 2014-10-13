.. _users_trouble:

Troubleshooting Varnish
=======================

Sometimes Varnish misbehaves or rather behaves the way you told it to behave but not necessarily the way you want it to behave. In order for you to understand whats
going on there are a couple of places you can check. `varnishlog`,
`/var/log/syslog`, `/var/log/messages` are all good places where Varnish might
leave clues of whats going on. This section will guide you through
basic troubleshooting in Varnish.


When Varnish won't start
------------------------

Sometimes Varnish wont start. There is a plethora of possible reasons why
Varnish wont start on your machine. We've seen everything from wrong
permissions on `/dev/null` to other processes blocking the ports.

Starting Varnish in debug mode to see what is going on.

Try to start Varnish by::

    # varnishd -f /usr/local/etc/varnish/default.vcl -s malloc,1G -T 127.0.0.1: 2000  -a 0.0.0.0:8080 -d

Notice the '-d' parameter. It will give you some more information on what
is going on. Let us see how Varnish will react when something else is
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

Now Varnish is running but only the master process is running, in debug
mode the cache does not start. Now you're on the console. You can
instruct the master process to start the cache by issuing "start".::

	 start
	 bind(): Address already in use
	 300 22
	 Could not open sockets

And here we have our problem. Something else is bound to the HTTP port
of Varnish. If this doesn't help try ``strace`` or ``truss`` or come find us
on IRC.


Varnish is crashing - panics
----------------------------

When Varnish goes bust the child processes crashes. Most of the
crashes are caught by one of the many consistency checks we have included in the Varnish source code. When Varnish hits one of these the caching
process will crash itself in a controlled manner, leaving a nice
stack trace with the mother process.

You can inspect any panic messages by typing ``panic.show`` in the CLI.::

 panic.show
 Last panic at: Tue, 15 Mar 2011 13:09:05 GMT
 Assert error in ESI_Deliver(), cache_esi_deliver.c line 354:
   Condition(i == Z_OK || i == Z_STREAM_END) not true.
 thread = (cache-worker)
 ident = Linux,2.6.32-28-generic,x86_64,-sfile,-smalloc,-hcritbit,epoll
 Backtrace:
   0x42cbe8: pan_ic+b8
   0x41f778: ESI_Deliver+438
   0x42f838: RES_WriteObj+248
   0x416a70: cnt_deliver+230
   0x4178fd: CNT_Session+31d
   (..)

The crash might be due to misconfiguration or a bug. If you suspect it
is a bug you can use the output in a bug report, see the "Trouble Tickets" section in the Introduction chapter above.

Varnish is crashing - segfaults
-------------------------------

Sometimes a bug escapes the consistency checks and Varnish gets hit
with a segmentation error. When this happens with the child process it
is logged, the core is dumped and the child process starts up again.

A core dumped is usually due to a bug in Varnish. However, in order to
debug a segfault the developers need you to provide a fair bit of
data.

 * Make sure you have Varnish installed with debugging symbols.
 * Make sure core dumps are allowed in the parent shell. (``ulimit -c unlimited``)

Once you have the core you open it with `gdb` and issue the command ``bt``
to get a stack trace of the thread that caused the segfault.


Varnish gives me Guru meditation
--------------------------------

First find the relevant log entries in `varnishlog`. That will probably
give you a clue. Since `varnishlog` logs a lot of data it might be hard
to track the entries down. You can set `varnishlog` to log all your 503
errors by issuing the following command::

   $ varnishlog -q 'RespStatus == 503' -g request

If the error happened just a short time ago the transaction might still
be in the shared memory log segment. To get `varnishlog` to process the
whole shared memory log just add the '-d' parameter::

   $ varnishlog -d -q 'RespStatus == 503' -g request

Please see the `vsl-query` and `varnishlog` man pages for elaborations
on further filtering capabilities and explanation of the various
options.


Varnish doesn't cache
---------------------

See :ref:`users-guide-increasing_your_hitrate`.

