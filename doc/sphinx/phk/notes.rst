..
	Copyright (c) 2016 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _phk_notes:

========================
Notes from the Architect
========================

Once you start working with the Varnish source code, you will notice
that Varnish is not your average run of the mill application.

That is not a coincidence.

I have spent many years working on the FreeBSD kernel, and only
rarely did I venture into userland programming, but when I had
occation to do so, I invariably found that people programmed like
it was still 1975.

So when I was approached about the Varnish project I wasn't really
interested until I realized that this would be a good opportunity
to try to put some of all my knowledge of how hardware and kernels
work to good use, and now that we have reached alpha stage, I can
say I have really enjoyed it.

So what's wrong with 1975 programming ?
---------------------------------------

The really short answer is that computers do not have two kinds of
storage any more.

It used to be that you had the primary store, and it was anything
from acoustic delaylines filled with mercury via small magnetic
dougnuts via transistor flip-flops to dynamic RAM.

And then there were the secondary store, paper tape, magnetic tape,
disk drives the size of houses, then the size of washing machines
and these days so small that girls get disappointed if think they
got hold of something else than the MP3 player you had in your
pocket.

And people program this way.

They have variables in "memory" and move data to and from "disk".

Take Squid for instance, a 1975 program if I ever saw one: You tell
it how much RAM it can use and how much disk it can use. It will
then spend inordinate amounts of time keeping track of what HTTP
objects are in RAM and which are on disk and it will move them forth
and back depending on traffic patterns.

Well, today computers really only have one kind of storage, and it
is usually some sort of disk, the operating system and the virtual
memory management hardware has converted the RAM to a cache for the
disk storage.

So what happens with squids elaborate memory management is that it
gets into fights with the kernels elaborate memory management, and
like any civil war, that never gets anything done.

What happens is this: Squid creates a HTTP object in "RAM" and it
gets used some times rapidly after creation. Then after some time
it get no more hits and the kernel notices this. Then somebody tries
to get memory from the kernel for something and the kernel decides
to push those unused pages of memory out to swap space and use the
(cache-RAM) more sensibly for some data which is actually used by
a program. This however, is done without squid knowing about it.
Squid still thinks that these http objects are in RAM, and they
will be, the very second it tries to access them, but until then,
the RAM is used for something productive.

This is what Virtual Memory is all about.

If squid did nothing else, things would be fine, but this is where
the 1975 programming kicks in.

After some time, squid will also notice that these objects are
unused, and it decides to move them to disk so the RAM can be used
for more busy data. So squid goes out, creates a file and then it
writes the http objects to the file.

Here we switch to the high-speed camera: Squid calls write(2), the
address i gives is a "virtual address" and the kernel has it marked
as "not at home".

So the CPU hardwares paging unit will raise a trap, a sort of
interrupt to the operating system telling it "fix the memory please".

The kernel tries to find a free page, if there are none, it will
take a little used page from somewhere, likely another little used
squid object, write it to the paging poll space on the disk (the
"swap area") when that write completes, it will read from another
place in the paging pool the data it "paged out" into the now unused
RAM page, fix up the paging tables, and retry the instruction which
failed.

Squid knows nothing about this, for squid it was just a single
normal memory access.

So now squid has the object in a page in RAM and written to the
disk two places: one copy in the operating systems paging space and
one copy in the filesystem.

Squid now uses this RAM for something else but after some time, the
HTTP object gets a hit, so squid needs it back.

First squid needs some RAM, so it may decide to push another HTTP
object out to disk (repeat above), then it reads the filesystem
file back into RAM, and then it sends the data on the network
connections socket.

Did any of that sound like wasted work to you ?

Here is how Varnish does it:

Varnish allocate some virtual memory, it tells the operating system
to back this memory with space from a disk file. When it needs to
send the object to a client, it simply refers to that piece of
virtual memory and leaves the rest to the kernel.

If/when the kernel decides it needs to use RAM for something else,
the page will get written to the backing file and the RAM page
reused elsewhere.

When Varnish next time refers to the virtual memory, the operating
system will find a RAM page, possibly freeing one, and read the
contents in from the backing file.

And that's it. Varnish doesn't really try to control what is cached
in RAM and what is not, the kernel has code and hardware support
to do a good job at that, and it does a good job.

Varnish also only has a single file on the disk whereas squid puts
one object in its own separate file. The HTTP objects are not needed
as filesystem objects, so there is no point in wasting time in the
filesystem name space (directories, filenames and all that) for
each object, all we need to have in Varnish is a pointer into virtual
memory and a length, the kernel does the rest.

Virtual memory was meant to make it easier to program when data was
larger than the physical memory, but people have still not caught
on.

More caches
-----------

But there are more caches around, the silicon mafia has more or
less stalled at 4GHz CPU clock and to get even that far they have
had to put level 1, 2 and sometimes 3 caches between the CPU and
the RAM (which is the level 4 cache), there are also things like
write buffers, pipeline and page-mode fetches involved, all to make
it a tad less slow to pick up something from memory.

And since they have hit the 4GHz limit, but decreasing silicon
feature sizes give them more and more transistors to work with,
multi-cpu designs have become the fancy of the world, despite the
fact that they suck as a programming model.

Multi-CPU systems is nothing new, but writing programs that use
more than one CPU at a time has always been tricky and it still is.

Writing programs that perform well on multi-CPU systems is even trickier.

Imagine I have two statistics counters:

        unsigned    n_foo;
        unsigned    n_bar;

So one CPU is chugging along and has to execute n_foo++

To do that, it read n_foo and then write n_foo back. It may or may
not involve a load into a CPU register, but that is not important.

To read a memory location means to check if we have it in the CPUs
level 1 cache. It is unlikely to be unless it is very frequently
used. Next check the level two cache, and let us assume that is a
miss as well.

If this is a single CPU system, the game ends here, we pick it out
of RAM and move on.

On a Multi-CPU system, and it doesn't matter if the CPUs share a
socket or have their own, we first have to check if any of the other
CPUs have a modified copy of n_foo stored in their caches, so a
special bus-transaction goes out to find this out, if if some cpu
comes back and says "yeah, I have it" that cpu gets to write it to
RAM. On good hardware designs, our CPU will listen in on the bus
during that write operation, on bad designs it will have to do a
memory read afterwards.

Now the CPU can increment the value of n_foo, and write it back.
But it is unlikely to go directly back to memory, we might need it
again quickly, so the modified value gets stored in our own L1 cache
and then at some point, it will end up in RAM.

Now imagine that another CPU wants to n_bar+++ at the same time,
can it do that ? No. Caches operate not on bytes but on some
"linesize" of bytes, typically from 8 to 128 bytes in each line.
So since the first cpu was busy dealing with n_foo, the second CPU
will be trying to grab the same cache-line, so it will have to wait,
even through it is a different variable.

Starting to get the idea ?

Yes, it's ugly.

How do we cope ?
----------------

Avoid memory operations if at all possible.

Here are some ways Varnish tries to do that:

When we need to handle a HTTP request or response, we have an array
of pointers and a workspace. We do not call malloc(3) for each
header. We call it once for the entire workspace and then we pick
space for the headers from there. The nice thing about this is that
we usually free the entire header in one go and we can do that
simply by resetting a pointer to the start of the workspace.

When we need to copy a HTTP header from one request to another (or
from a response to another) we don't copy the string, we just copy
the pointer to it. Provided we do not change or free the source
headers, this is perfectly safe, a good example is copying from the
client request to the request we will send to the backend.

When the new header has a longer lifetime than the source, then we
have to copy it. For instance when we store headers in a cached
object. But in that case we build the new header in a workspace,
and once we know how big it will be, we do a single malloc(3) to
get the space and then we put the entire header in that space.

We also try to reuse memory which is likely to be in the caches.

The worker threads are used in "most recently busy" fashion, when
a workerthread becomes free it goes to the front of the queue where
it is most likely to get the next request, so that all the memory
it already has cached, stack space, variables etc, can be reused
while in the cache, instead of having the expensive fetches from
RAM.

We also give each worker thread a private set of variables it is
likely to need, all allocated on the stack of the thread. That way
we are certain that they occupy a page in RAM which none of the
other CPUs will ever think about touching as long as this thread
runs on its own CPU. That way they will not fight about the cachelines.

If all this sounds foreign to you, let me just assure you that it
works: we spend less than 18 system calls on serving a cache hit,
and even many of those are calls tog get timestamps for statistics.

These techniques are also nothing new, we have used them in the
kernel for more than a decade, now it's your turn to learn them :-)

So Welcome to Varnish, a 2006 architecture program.

*phk*
