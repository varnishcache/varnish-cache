.. _phk_cheri_2:

How Varnish met CHERI 2/N
=========================

CHERI capabilities are twice the size of pointers, and Varnish not
only uses a lot of pointers per request, it is also stingy with
RAM, because it is not uncommon to use 100K worker threads.

A number of test-cases fail because they are stingy with memory
allocations, and other test-cases run out of cache space.

The job of these test-cases is to push varnish into obscure code-paths,
using finely tuned sizes of objects, lengths of headers and parameter
settings to varnish, and the bigger pointers in Varnish trows that
tuning off.

These test-failures have nothing to do with CHERI.

There was enough margin that we could find magic numbers which work
on both 32 bit and 64 bit CPUs, that is with both 4 and 8 byte
pointers, but it is doubtful there is enough margin to make them
also work with 16 byte pointers, so I will merely list these tests
here as part of the accounting::

    Workspace sizes
    =====================
    TEST tests/c00108.vtc
    TEST tests/r01038.vtc
    TEST tests/r01120.vtc
    TEST tests/r02219.vtc
    TEST tests/o00005.vtc

    Cache sizes
    =====================
    TEST tests/r03502.vtc
    TEST tests/r01140.vtc
    TEST tests/r02831.vtc
    TEST tests/v00064.vtc

Things you cannot do under CHERI: Pointers in Pipes
---------------------------------------------------

Varnish has a central "waiter" service, whose job it is to monitor
file descriptors to idle network connections, and do the right thing
if data arrives on them, or if they are, or should be closed after
a timeout.

For reasons of performance, we have multiple implementations:
``kqueue(2)`` (BSD), ``epoll(2)`` (Linux), ``ports(2)`` (Solaris)
and ``poll(2)`` which should work everywhere POSIX has been read.

We only have the ``poll(2)`` based waiter for portability, one
less issue to deal with during bring-up on new platforms, its
performance degrades to uselessness with contemporary loads
of open network connections.

The way they all work is that have a single thread sitting
in the relevant system-call, monitoring tens of thousands
of file descriptors.

Some of those system calls allows other threads to add fds to the
list, but ``poll(2)`` does not, so when we start the poll-waiter
we create a ``pipe(2)``, and have the waiter-thread listen to that
too.

When another thread wants to add a file descriptor to the inventory,
it uses ``write(2)`` to send a pointer into that pipe.  The kernel
provide all the locking and buffering for us, wakes up the waiter-thread
which reads the pointer, adds the new fd to its inventory and dives
back into ``poll(2)``.

This is 100% safe, because nobody else can get to a pipe created
with ``pipe(2)``, but there is no way CHERI could spot that to
make an execption, so reading pointers out of a filedescriptor,
cause fully justified core-dumps.

If the poll-waiter was actually relevant, the proper fix would be
to let the sending thread stick things on a locked list and just
write a nonce-byte into the pipe to the waiter-thread, but that
goes at the bottom of the TODO list, and for now I just remove the
-Wpoll argument from five tests, which then pass::

    -Wpoll
    =====================
    TEST tests/b00009.vtc
    TEST tests/b00048.vtc
    TEST tests/b00054.vtc
    TEST tests/b00059.vtc
    TEST tests/c00080.vtc

But why five tests ?

It looks like one to test the poll-waiter and four cases of copy&paste.

Never write your own Red-Black Trees
------------------------------------

In general there are few pieces of code I dare not wade into,
but there are a LOT of code I dont want to touch, if there
is any way to avoid it.

Red-Black trees are one of them.

In Varnish we stol^H^H^H^H imported both ``<queue.h>`` and ``<tree.h>``
from FreeBSD, but as a safety measure we stuck a ``V`` prefix on
everything in them.

Every so often I will run a small shell-script which does the
v-thing and compare the result to ``vtree.h`` and ``vqueue.h``,
to keep up with FreeBSD.

Today that paid off handsomely:  Some poor person on the CHERI
team had to wade into ``tree.h`` and stick ``__no_subobject_bounds``
directives to pointers to make that monster work under CHERI.

I just ran my script and 20 more tests pass::

    Red-Black Trees
    =====================
    TEST tests/b00068.vtc
    TEST tests/c00005.vtc
    TEST tests/e00003.vtc
    TEST tests/e00008.vtc
    TEST tests/e00019.vtc
    TEST tests/l00002.vtc
    TEST tests/l00003.vtc
    TEST tests/l00005.vtc
    TEST tests/m00053.vtc
    TEST tests/r01312.vtc
    TEST tests/r01441.vtc
    TEST tests/r02451.vtc
    TEST tests/s00012.vtc
    TEST tests/u00004.vtc
    TEST tests/u00010.vtc
    TEST tests/v00009.vtc
    TEST tests/v00011.vtc
    TEST tests/v00017.vtc
    TEST tests/v00041.vtc
    TEST tests/v00043.vtc

Four failures left…

*/phk*
