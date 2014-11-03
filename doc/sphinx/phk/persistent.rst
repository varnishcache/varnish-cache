.. _phk_pesistent:

====================
A persistent message
====================

This message is about -spersistent and why you should not use it,
even though it is still present in Varnish 4.x.

TL;DR:
------

Under narrow and ill defined circumstances, -spersistent works well,
but in general it is more trouble than it is worth for you to run
it, and we don't presently have the development resources to fix that.

If you think you have these circumstances, you need to specify

	-sdeprecated_persistence

in order to use it.

The long story
--------------

When we added -spersistent, to Varnish, it was in response to, and
sponsored by a specific set of customers who really wanted this.

A persistent storage module is an entirely different kettle of vax
than a non-persistent module, because of all the ugly consistency
issues it raises.

Let me give you an example.

Imagine a cluster of some Varnish servers on which bans are used.

Without persistent storage, if one of them goes down and comes back
up, all the old cached objects are gone, and so are, by definition
all the banned objects.

With persistent storage, we not only have to store the still live
bans with the cached objects, and keep the two painfully in sync,
so the bans gets revived with the objects, we also have to worry
about missing bans during the downtime, since those might ban objects
we will recover on startup.

Ouch:  Straight into database/filesystem consistency territory.

But we knew that, and I thought I had a good strategy to deal with
this.

And in a sense I did.

Varnish has the advantage over databases and filesystems that we
can actually loose objects without it being a catastrophy.  It would
be better if we didn't, but we can simply ditch stuff which doesn't
look consistent and we'll be safe.

The strategy was to do a "Log Structured Filesystem", a once promising
concept which soon proved very troublesome to implement well.

Interestingly, today the ARM chip in your SSD most likely implements
a LFS for wear-levelling, but with a vastly reduced feature set:
All "files" are one sector long, filenames are integers and there
are no subdirectories or rename operations.  On the other hand,
there is extra book-keeping about the state of the flash array.

A LFS consists of two major components:  The bit that reads and
writes, which is pretty trivial, and the bit which makes space
available which isn't.

Initially we didn't even do the second part, because in varnish
objects expire, and provided they do so fast enough, the space will
magically make itself available.  This worked well enough for our
initial users, and they only used bans sporadically so that was
cool too.

In other words, a classic 20% effort, 80% benefit.

Unfortunately we have not been able to find time and money for the
other 80% effort which gives the last 20% benefit, and therefor
-spersistent has ended up in limbo.

Today we decided to officially deprecate -spersistent, and start
warning people against using it, but we will leave it in the source
code for now, in order to keep the interfaces necessary for a
persistent storage working, in the hope that we will get to use
them again later.

So you can still use persistent storage, if you really want to,
and if you know what you're doing, by using:

	-sdeprecated_persistent

You've been warned.


Poul-Henning, 2014-05-26
