%%%%%%%%%%%%
Introduction
%%%%%%%%%%%%

Most tutorials are written in "subject-order", as the old Peanuts
strip goes::

	Jogging: A Handbook
	Author:  S. Noopy
	    Chapter 1: Left foot
		It was a dark and stormy night...

This is great when the reader has no choice, and nothing better to
do, but read the entire document before starting.

We have taken the other approach: "breadth-first", because experience
has shown us that Varnish users wants to get things running, and then
polish up things later on.

With that in mind, we have written the tutorial so you can break off,
as Calvin tells Ms. Wormwood, "when my brain is full for today", and
come back later and learn more.

That also means that right from the start, we will have several
things going on in parallel and you will need at least four, sometimes
more, terminal windows at the same time, to run the examples.

A word about TCP ports
----------------------

We have subverted our custom built regression test tool, a program
called ```varnishtest``` to help you get through these examples,
without having to waste too much time setting up webservers as 
backends or browsers as clients, to drive the examples.

But there is one complication we can not escape:  TCP port numbers.

Each of the backends we simulate and the varnishd instances we run
needs a TCP port number to listen to and it is your job to find them,
because we have no idea what servers are running on your computer
nor what TCP ports they use.

To make this as easy as possible, we have implemented a ```-L
number``` argument to all the varnish programs, which puts them in
"Learner" mode, and in all the examples we have used 20000 as
the number, because on most systems the middle of the range
(1000...65000) is usually not used.

If these ports are in use on your system (is your colleague also running
the Varnish tutorial ?) simply pick another number and use that
instead of 20000.
