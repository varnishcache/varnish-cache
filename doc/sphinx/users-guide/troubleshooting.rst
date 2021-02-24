..
	Copyright (c) 2012-2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _users_trouble:

Troubleshooting Varnish
=======================

Sometimes Varnish misbehaves or rather behaves the way you told it to
behave but not necessarily the way you want it to behave. In order for
you to understand whats going on there are a couple of places you can
check. :ref:`varnishlog(1)`, ``/var/log/syslog``,
``/var/log/messages`` are all good places where Varnish might leave
clues of whats going on. This section will guide you through basic
troubleshooting in Varnish.


When Varnish won't start
------------------------

Sometimes Varnish wont start. There is a plethora of possible reasons why
Varnish wont start on your machine. We've seen everything from wrong
permissions on ``/dev/null`` to other processes blocking the ports.

Starting Varnish in debug mode to see what is going on.

Try to start Varnish with the same arguments as otherwise, but ``-d``
added. This will give you some more information on what is going
on. Let us see how Varnish will react when something else is listening
on its port.::

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
crashes are caught by one of the many consistency checks we have
included in the Varnish source code. When Varnish hits one of these
the caching process will crash itself in a controlled manner, leaving
a nice stack trace with the mother process.

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
is a bug you can use the output in a bug report, see the "Trouble
Tickets" section in the Introduction chapter above.

Varnish is crashing - stack overflows
-------------------------------------

Bugs put aside, the most likely cause of crashes are stack overflows,
which is why we have added a heuristic to add a note when a crash
looks like it was caused by one. In this case, the panic message
contains something like this::

	Signal 11 (Segmentation fault) received at 0x7f631f1b2f98 si_code 1
	THIS PROBABLY IS A STACK OVERFLOW - check thread_pool_stack parameter

as a first measure, please follow this advise and check if crashes
still occur when you add 128k to whatever the value of the
``thread_pool_stack`` parameter and restart varnish.

If varnish stops crashing with a larger ``thread_pool_stack``
parameter, it's not a bug (at least most likely).

Varnish is crashing - segfaults
-------------------------------

Sometimes a bug escapes the consistency checks and Varnish gets hit
with a segmentation error. When this happens with the child process it
is logged, the core is dumped and the child process starts up again.

A core dumped is usually due to a bug in Varnish. However, in order to
debug a segfault the developers need you to provide a fair bit of
data.

 * Make sure you have Varnish installed with debugging symbols.
 * Check where your operating system writes core files and ensure that
   you actually get them. For example on linux, learn about
   ``/proc/sys/kernel/core_pattern`` from the `core(5)` manpage.
 * Make sure core dumps are allowed in the parent shell from which
   varnishd is being started. In shell, this would be::

	ulimit -c unlimited

   but if varnish is started from an init-script, that would need to
   be adjusted or in the case of systemd, ``LimitCORE=infinity`` set
   in the service's ``[Service]]`` section of the unit file.

Once you have the core, ``cd`` into varnish's working directory (as
given by the ``-n`` parameter, whose default is
``$PREFIX/var/varnish/$HOSTNAME`` with ``$PREFIX`` being the
installation prefix, usually ``/usr/local``, open the core with
``gdb`` and issue the command ``bt`` to get a stack trace of the
thread that caused the segfault.

A basic debug session for varnish installed under ``/usr/local`` could look
like this::

	$ cd /usr/local/var/varnish/`uname -n`/
	$ gdb /usr/local/sbin/varnishd core
	GNU gdb (Debian 7.12-6) 7.12.0.20161007-git
	Copyright (C) 2016 Free Software Foundation, Inc.
	[...]
	Core was generated by `/usr/local/sbin/varnishd -a 127.0.0.1:8080 -b 127.0.0.1:8080'.
	Program terminated with signal SIGABRT, Aborted.
	#0  __GI_raise (sig=sig@entry=6) at ../sysdeps/unix/sysv/linux/raise.c:51
	51	../sysdeps/unix/sysv/linux/raise.c: No such file or directory.
	[Current thread is 1 (Thread 0x7f7749ea3700 (LWP 31258))]

	(gdb) bt
	#0  __GI_raise (sig=sig@entry=6) at ../sysdeps/unix/sysv/linux/raise.c:51
	#1  0x00007f775132342a in __GI_abort () at abort.c:89
	#2  0x000000000045939f in pan_ic (func=0x7f77439fb811 "VCL", file=0x7f77439fb74c "", line=0,
	    cond=0x7f7740098130 "PANIC: deliberately!", kind=VAS_VCL) at cache/cache_panic.c:839
	#3  0x0000000000518cb1 in VAS_Fail (func=0x7f77439fb811 "VCL", file=0x7f77439fb74c "", line=0,
	    cond=0x7f7740098130 "PANIC: deliberately!", kind=VAS_VCL) at vas.c:51
	#4  0x00007f77439fa6e9 in vmod_panic (ctx=0x7f7749ea2068, str=0x7f7749ea2018) at vmod_vtc.c:109
	#5  0x00007f77449fa5b8 in VGC_function_vcl_recv (ctx=0x7f7749ea2068) at vgc.c:1957
	#6  0x0000000000491261 in vcl_call_method (wrk=0x7f7749ea2dd0, req=0x7f7740096020, bo=0x0,
	    specific=0x0, method=2, func=0x7f77449fa550 <VGC_function_vcl_recv>) at cache/cache_vrt_vcl.c:462
	#7  0x0000000000493025 in VCL_recv_method (vcl=0x7f775083f340, wrk=0x7f7749ea2dd0, req=0x7f7740096020,
	    bo=0x0, specific=0x0) at ../../include/tbl/vcl_returns.h:192
	#8  0x0000000000462979 in cnt_recv (wrk=0x7f7749ea2dd0, req=0x7f7740096020) at cache/cache_req_fsm.c:880
	#9  0x0000000000461553 in CNT_Request (req=0x7f7740096020) at ../../include/tbl/steps.h:36
	#10 0x00000000004a7fc6 in HTTP1_Session (wrk=0x7f7749ea2dd0, req=0x7f7740096020)
	    at http1/cache_http1_fsm.c:417
	#11 0x00000000004a72c3 in http1_req (wrk=0x7f7749ea2dd0, arg=0x7f7740096020)
	    at http1/cache_http1_fsm.c:86
	#12 0x0000000000496bb6 in Pool_Work_Thread (pp=0x7f774980e140, wrk=0x7f7749ea2dd0)
	    at cache/cache_wrk.c:406
	#13 0x00000000004963e3 in WRK_Thread (qp=0x7f774980e140, stacksize=57344, thread_workspace=2048)
	    at cache/cache_wrk.c:144
	#14 0x000000000049610b in pool_thread (priv=0x7f774880ec80) at cache/cache_wrk.c:439
	#15 0x00007f77516954a4 in start_thread (arg=0x7f7749ea3700) at pthread_create.c:456
	#16 0x00007f77513d7d0f in clone () at ../sysdeps/unix/sysv/linux/x86_64/clone.S:97



Varnish gives me Guru meditation
--------------------------------

First find the relevant log entries in :ref:`varnishlog(1)`. That will
probably give you a clue. Since :ref:`varnishlog(1)` logs a lot of
data it might be hard to track the entries down. You can set
:ref:`varnishlog(1)` to log all your 503 errors by issuing the
following command::

   $ varnishlog -q 'RespStatus == 503' -g request

If the error happened just a short time ago the transaction might
still be in the shared memory log segment. To get :ref:`varnishlog(1)`
to process the whole shared memory log just add the '-d' parameter::

   $ varnishlog -d -q 'RespStatus == 503' -g request

Please see the :ref:`vsl-query(7)` and :ref:`varnishlog(1)` man pages
for elaborations on further filtering capabilities and explanation of
the various options.


Varnish doesn't cache
---------------------

See :ref:`users-guide-increasing_your_hitrate`.
