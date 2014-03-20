%%%%%%%%%%%%%%
Reporting bugs
%%%%%%%%%%%%%%

Varnish can be a tricky beast to debug, having potentially thousands
of threads crowding into a few data structures makes for *interesting*
core dumps.

Actually, let me rephrase that without irony:  You tire of the "no,
not thread 438 either, lets look at 439 then..." routine really fast.

So if you run into a bug, it is important that you spend a little bit
of time collecting the right information, to help us fix the bug.

The most valuable information you can give us, is **always** how
to trigger and reproduce the problem. If you can tell us that, we
rarely need anything else to solve it.The caveat being, that we
do not have a way to simulate high levels of real-life web-traffic,
so telling us to "have 10.000 clients hit at once" does not really
allow us to reproduce.

To report a bug please follow the suggested procedure described in the "Trouble Tickets" 
section of the documentation (above).

Roughly we categorize bugs in to three kinds of bugs (described below) with Varnish. The information
we need to debug them depends on what kind of bug we are facing.

Varnish crashes
===============

Plain and simple: **boom**

Varnish is split over two processes, the manager and the child.  The child
does all the work, and the manager hangs around to resurrect it if it
crashes.

Therefore, the first thing to do if you see a Varnish crash, is to examine
your syslogs to see if it has happened before. (One site is rumoured
to have had Varnish restarting every 10 minutes and *still* provide better
service than their CMS system.)

When it crashes, which is highly unlikely to begin with, Varnish will spew out a crash dump
that looks something like::

	Child (32619) died signal=6 (core dumped)
	Child (32619) Panic message: Assert error in ccf_panic(), cache_cli.c line 153:
	  Condition(!strcmp("", "You asked for it")) not true.
	errno = 9 (Bad file descriptor)
	thread = (cache-main)
	ident = FreeBSD,9.0-CURRENT,amd64,-sfile,-hcritbit,kqueue
	Backtrace:
	  0x42bce1: pan_ic+171
	  0x4196af: ccf_panic+4f
	  0x8006b3ef2: _end+80013339a
	  0x8006b4307: _end+8001337af
	  0x8006b8b76: _end+80013801e
	  0x8006b8d84: _end+80013822c
	  0x8006b51c1: _end+800134669
	  0x4193f6: CLI_Run+86
	  0x429f8b: child_main+14b
	  0x43ef68: start_child+3f8
	[...]

If you can get that information to us, we are usually able to
see exactly where things went haywire, and that speeds up bugfixing
a lot.

There will be a lot more information in the crash dump besides this, and before sending
it all to us, you should obscure any sensitive/secret
data/cookies/passwords/ip# etc.  Please make sure to keep context
when you do so, ie: do not change all the IP# to "X.X.X.X", but
change each IP# to something unique, otherwise we are likely to be
more confused than informed.

The most important line is the "Panic Message", which comes in two
general forms:

"Missing errorhandling code in ..."
	This is a situation where we can conceive Varnish ending up, which we have not
	(yet) written the padded-box error handling code for.

	The most likely cause here, is that you need a larger workspace
	for HTTP headers and Cookies.

	Please try that before reporting a bug.

"Assert error in ..."
	This is something bad that should never happen, and a bug
	report is almost certainly in order. As always, if in doubt
	ask us on IRC before opening the ticket.

..  (TODO: in the ws-size note above, mention which params to tweak)

In your syslog it may all be joined into one single line, but if you
can reproduce the crash, do so while running `varnishd` manually:

	``varnishd -d <your other arguments> |& tee /tmp/_catch_bug``

That will get you the entire panic message into a file.

(Remember to type ``start`` to launch the worker process, that is not
automatic when ``-d`` is used.)

Varnish goes on vacation
========================

This kind of bug is nasty to debug, because usually people tend to
kill the process and send us an email saying "Varnish hung, I
restarted it" which gives us only about 1.01 bit of usable debug
information to work with.

What we need here is all the information you can squeeze out of
your operating system **before** you kill the Varnish process.

One of the most valuable bits of information, is if all Varnish'
threads are waiting for something or if one of them is spinning
furiously on some futile condition.

Commands like ``top -H`` or ``ps -Haxlw`` or ``ps -efH`` should be
able to figure that out.

.. XXX:Maybe a short description of what valuable information the various commands above generates? /benc 


If one or more threads are spinning, use ``strace`` or ``ktrace`` or ``truss``
(or whatever else your OS provides) to get a trace of which system calls
the Varnish process issues. Be aware that this may generate a lot
of very repetitive data, usually one second worth of data is more than enough.

Also, run ``varnishlog`` for a second, and collect the output
for us, and if ``varnishstat`` shows any activity, capture that also.

When you have done this, kill the Varnish *child* process, and let
the *master* process restart it.  Remember to tell us if that does
or does not work. If it does not, kill all Varnish processes, and
start from scratch. If that does not work either, tell us, that
means that we have wedged your kernel.


Varnish does something wrong
============================

These are the easy bugs: usually all we need from you is the relevant
transactions recorded with ``varnishlog`` and your explanation of
what is wrong about what Varnish does.

Be aware, that often Varnish does exactly what you asked it to, rather
than what you intended it to do. If it sounds like a bug that would
have tripped up everybody else, take a moment to read through your
VCL and see if it really does what you think it does.

You can also try setting the ``vcl_trace`` parameter, that will generate log
records with like and character number for each statement executed in your VCL
program.

.. XXX:Example of the command perhaps? benc

