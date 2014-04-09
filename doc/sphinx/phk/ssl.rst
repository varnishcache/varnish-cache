.. _phk_ssl:

============
Why no SSL ?
============

This is turning into a bit of a FAQ, but the answer is too big to fit
in the margin we use for those.

There are a number of reasons why there are no plans in sight that will
grow SSL support in Varnish.

First, I have yet to see a SSL library where the source code is not
a nightmare.

As I am writing this, the varnish source-code tree contains 82.595
lines of .c and .h files, including JEmalloc (12.236 lines) and
Zlib (12.344 lines).

OpenSSL, as imported into FreeBSD, is 340.722 lines of code, nine
times larger than the Varnish source code, 27 times larger than
each of Zlib or JEmalloc.

This should give you some indication of how insanely complex
the canonical implementation of SSL is.

Second, it is not exactly the best source-code in the world.  Even
if I have no idea what it does, there are many aspect of it that
scares me.

Take this example in a comment, randomly found in s3-srvr.c::

	/* Throw away what we have done so far in the current handshake,
	 * which will now be aborted. (A full SSL_clear would be too much.)
	 * I hope that tmp.dh is the only thing that may need to be cleared
	 * when a handshake is not completed ... */

I hope they know what they are doing, but this comment doesn't exactly
carry that point home, does it ?

But let us assume that a good SSL library can be found, what would
Varnish do with it ?

We would terminate SSL sessions, and we would burn CPU cycles doing
that.  You can kiss the highly optimized delivery path in Varnish
goodbye for SSL, we cannot simply tell the kernel to put the bytes
on the socket, rather, we have to corkscrew the data through
the SSL library and then write it to the socket.

Will that be significantly different, performance wise, from running
a SSL proxy in separate process ?

No, it will not, because the way varnish would have to do it would
be to ... start a separate process to do the SSL handling.

There is no other way we can guarantee that secret krypto-bits do
not leak anywhere they should not, than by fencing in the code that
deals with them in a child process, so the bulk of varnish never
gets anywhere near the certificates, not even during a core-dump.

Would I be able to write a better stand-alone SSL proxy process
than the many which already exists ?

Probably not, unless I also write my own SSL implementation library,
including support for hardware crypto engines and the works.

That is not one of the things I dreamt about doing as a kid and
if I dream about it now I call it a nightmare.

So the balance sheet, as far as I can see it, lists "It would be
a bit easier to configure" on the plus side, and everything else
piles up on the minus side, making it a huge waste of time
and effort to even think about it..

Poul-Henning, 2011-02-15
