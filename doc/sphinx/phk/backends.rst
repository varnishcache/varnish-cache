.. _phk_backends:

===============================
What do you mean by 'backend' ?
===============================

Given that we are approaching Varnish 3.0, you would think I had this
question answered conclusively long time ago, but once you try to
be efficient, things get hairy fast.

One of the features of Varnish we are very fundamental about, is the
ability to have multiple VCLs loaded at the same time, and to switch
between them instantly and seamlessly.

So imagine you have 1000 backends in your VCL, not an unreasonable
number, each configured with health-polling.

Now you fiddle your vcl_recv{} a bit and load the VCL again, but
since you are not sure which is the best way to do it, you keep
both VCL's loaded so you can switch forth and back seamlessly.

To switch seamlessly, the health status of each backend needs to
be up to date the instant we switch to the other VCL.

This basically means that either all VCLs poll all their backends,
or they must share, somehow.

We can dismiss the all VCLs poll all their backends scenario,
because it scales truly horribly, and would pummel backends with
probes if people forget to vcl.discard their old dusty VCLs.

Share And Enjoy
===============

In addition to health-status (including the saint-list), we also
want to share cached open connections and stats counters.

It would be truly stupid to close 100 ready and usable connections to
a backend, and open 100 other, just because we switch to a different
VCL that has an identical backend definition.

But what is an identical backend definition in this context?

It is important to remember that we are not talking physical
backends:  For instance, there is nothing preventing a VCL for
having the same physical backend declared as 4 different VCL
backends.

The most obvious thing to do, is to use the VCL name of the backend
as identifier, but that is not enough.  We can have two different
VCLs where backend "b1" points at two different physical machines,
for instance when we migrate or upgrade the backend.

The identity of the state than can be shared is therefore the triplet:
	{VCL-name, IPv4+port, IPv6+port} 

No Information without Representation
=====================================

Since the health-status will be for each of these triplets, we will
need to find a way to represent them in CLI and statistics contexts.

As long as we just print them out, that is not a big deal, but what
if you just want the health status for one of your 1000 backends,
how do you tell which one ?

The syntax-nazi way of doing that, is forcing people to type it all
every time::

	backend.health b1(127.0.0.1:8080,[::1]:8080)

That will surely not be a hit with people who have just one backend.

I think, but until I implement I will not commit to, that the solution
is a wildcard-ish scheme, where you can write things like::

	b1				# The one and only backend b1 or error

	b1()				# All backends named b1

	b1(127.0.0.1)			# All b1s on IPv4 lookback

	b1(:8080)			# All b1s on port 8080, (IPv4 or IPv6)

	b1(192.168.60.1,192.168.60.2)	# All b1s on one of those addresses.

(Input very much welcome)

The final question is if we use shortcut notation for output from
varnishd, and the answer is no, because we do not want the stats-counters
to change name because we load another VCL and suddenly need disabiguation.


Sharing Health Status
=====================

To avoid the over-polling, we define that maximum one VCL polls at
backend at any time, and the active VCL gets preference.  It is not
important which particular VCL polls the backends not in the active
VCL, as long as one of them do.

Implementation
==============

The poll-policy can be implemented by updating a back-pointer to
the poll-specification for all backends on vcl.use execution.

On vcl.discard, if this vcl was the active poller, it needs to walk
the list of vcls and substitute another.  If the list is empty
the backend gets retired anyway.

We should either park a thread on each backend, or have a poller thread
which throws jobs into the work-pool as the backends needs polled.

The pattern matching is confined to CLI and possibly libvarnishapi

I think this will work,

Until next time,

Poul-Henning, 2010-08-09
