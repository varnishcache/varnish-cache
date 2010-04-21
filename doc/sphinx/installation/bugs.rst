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

Roughly we have three clases of bugs with Varnish.

Varnish crashes
===============

Plain and simple: **boom**

Varnish is split over two processes, the manager and the child.  The child
does all the work, and the manager hangs around to resurect it, if it
crashes.

Therefore, the first thing to do if you see a varnish crash, is to examine
your syslogs, to see if it has happened before.  (One site is rumoured
to have had varnish restarting every 10 minutes and *still* provide better
service than their CMS system.)

When it crashes, if at all possible, Varnish will spew out a crash dump
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

There will be a lot more information than this, and before sending
it all to us, you should obscure any sensitive/secret
data/cookies/passwords/ip# etc.  Please make sure to keep context
when you do so, ie: do not change all the IP# to "X.X.X.X", but
change each IP# to something unique, otherwise we are likely to be
more confused than informed.

The most important line is the "Panic Message", which comes in two
general forms:

"Missing errorhandling code in ..."
	This is a place where we can conceive ending up, and have not
	(yet) written the padded-box error handling code for.

	The most likely cause here, is that you need a larger workspace
	for HTTP headers and Cookies. (XXX: which params to tweak)

	Please try that before reporting a bug.

"Assert error in ..."
	This is something bad that should never happen, and a bug
	report is almost certainly in order.  As always, if in doubt
	ask us on IRC before opening the ticket.

In your syslog it may all be joined into one single line, but if you
can reproduce the crash, do so while running varnishd manually:

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

What we need here is all the information can you squeeze out of
your operating system **before** you kill the Varnish process.

One of the most valuable bits of information, is if all Varnish'
threads are waiting for something or if one of them is spinning
furiously on some futile condition.

Commands like ``top -H`` or ``ps -Haxlw`` or ``ps -efH`` should be
able to figure that out.

If one or more threads are spinning, use ``strace`` or ``ktrace`` or ``truss``
(or whatever else your OS provides) to get a trace of which system calls
the varnish process issues.  Be aware that this may generate a lot
of very repetitive data, usually one second worth is more than enough.

Also, run ``varnishlog`` for a second, and collect the output
for us, and if ``varnishstat`` shows any activity, capture that also.

When you have done this, kill the Varnish *child* process, and let
the *master* process restart it.  Remember to tell us if that does
or does not work.  If it does not, kill all Varnish processes, and
start from scratch.  If that does not work either, tell us, that
means that we have wedged your kernel.



