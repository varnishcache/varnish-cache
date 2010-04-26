.. _Tutorial:

%%%%%%%%%%%%%%%%
Varnish Tutorial
%%%%%%%%%%%%%%%%

Welcome to the Varnish Tutorial, we hope this will help you get to 
know and understand Varnish.

Most tutorials are written in "subject-order", as the old Peanuts
strip goes::

	Jogging: A Handbook
	Author:  S. Noopy
	    Chapter 1: Left foot
		It was a dark and stormy night...

This is great when the reader has no choice, or nothing better to do, but
read the entire document before starting.

We have taken the other approach: "breadth-first", because experience
has shown us that Varnish users wants to get things running, and then
polish up things later on.

With that in mind, we have written the tutorial so you can break off,
as Calvin tells Ms. Wormwood, "when my brain is full for today", and
come back later and learn more.

That also means that right from the start, we will have several
things going on in parallel and you will need at least four, sometimes
more, terminal windows at the same time, to run the examples.


//todo// First simple example (pending varnishtest support)


.. todo::
        starting varnish with -d, seeing a transaction go through
        explain varnishlog output for a miss and a hit
        a few simple VCL tricks, including switching VCL on the fly
        The helpers: varnishstat, varnishhist, varnishtop varnishncsa
        Now that you know how it works, lets talk planning:
        - backend, directors and polling
        - storage
        - logging
        - management CLI & security
        - ESI
        Real life examples:
        - A real life varnish explained
        - A more complex real life varnish explained
        - Sky's Wikia Setup
        Varnishtest
        - What varnishtest does and why
        - writing simple test-cases
        - using varnishtest to test your VCL
        - using varnishtest to reproduce bugs

