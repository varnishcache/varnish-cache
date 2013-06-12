.. _phk_varnish_does_not_hash:

=====================
Varnish Does Not Hash
=====================

A spate of security advisories related to hash-collisions have made
a lot of people stare at Varnish and wonder if it is affected.

The answer is no, but the explanation is probably not what most of
you expected:

Varnish does not hash, at least not by default, and
even if it does, it's still as immune to the attacks as can be.

To understand what is going on, I have to introduce a concept from
Shannon's information theory: "entropy."

Entropy is hard to explain, and according to legend, that is exactly
why Shannon recycled that term from thermodynamics.

In this context, we can get away with thinking about entropy as how
much our "keys" differ::

	Low entropy (1 bit):
		/foo/bar/barf/some/cms/content/article?article=2
		/foo/bar/barf/some/cms/content/article?article=3

	High entropy (65 bits):
		/i?ee30d0770eb460634e9d5dcfb562a2c5.html
		/i?bca3633d52607f38a107cb5297fd66e5.html

Hashing consists of calculating a hash-index from the key and
storing the objects in an array indexed by that key.

Typically, but not always, the key is a string and the index is a
(smallish) integer, and the job of the hash-function is to squeeze
the key into the integer, without losing any of the entropy.

Needless to say, the more entropy you have to begin with, the more
of it you can afford to lose, and lose some you almost invariably
will.

There are two families of hash-functions, the fast ones, and the good
ones, and the security advisories are about the fast ones.

The good ones are slower, but probably not so much slower that you
care, and therefore, if you want to fix your web-app:

Change::
	foo=somedict[$somekey]
To::
	foo=somedict[md5($somekey)]

and forget about the advisories.

Yes, that's right: Cryptographic hash algorithms are the good ones,
they are built to not throw any entropy away, and they are built to
have very hard to predict collisions, which is exactly the problem
with the fast hash-functions in the advisories.

-----------------
What Varnish Does
-----------------

The way to avoid having hash-collisions is to not use a hash:  Use a
tree instead. There every object has its own place and there are no
collisions.

Varnish does that, but with a twist.

The "keys" in Varnish can be very long; by default they consist of::

	sub vcl_hash {
	    hash_data(req.url);
	    if (req.http.host) {
		hash_data(req.http.host);
	    } else {
		hash_data(server.ip);
	    }
	    return (hash);
	}

But some users will add cookies, user identification and many other
bits and pieces of string in there, and in the end the keys can be
kilobytes in length, and quite often, as in the first example above,
the first difference may not come until pretty far into the keys.

Trees generally need to have a copy of the key around to be able
to tell if they have a match, and more importantly to compare
tree-leaves in order to "re-balance" the tree and other such arcanae
of data structures.

This would add another per-object memory load to Varnish, and it
would feel particularly silly to store 48 identical characters for
each object in the far too common case seen above.

But furthermore, we want the tree to be very fast to do lookups in,
preferably it should be lockless for lookups, and that means that
we cannot (realistically) use any of the "smart" trees which
automatically balance themselves, etc.

You (generally) don't need a "smart" tree if your keys look
like random data in the order they arrive, but we can pretty
much expect the opposite as article number 4, 5, 6 etc are added
to the CMS in the first example.

But we can make the keys look random, and make them small and fixed
size at the same time, and the perfect functions designed for just
that task are the "good" hash-functions, the cryptographic ones.

So what Varnish does is "key-compression":  All the strings fed to
hash_data() are pushed through a cryptographic hash algorithm called
SHA256, which, as the name says, always spits out 256 bits (= 32
bytes), no matter how many bits you feed it.

This does not eliminate the key-storage requirement, but now all
the keys are 32 bytes and can be put directly into the data structure::

	struct objhead {
		[...]
		unsigned char           digest[DIGEST_LEN];
	};

In the example above, the output of SHA256 for the 1 bit difference
in entropy becomes::

	/foo/bar/barf/some/cms/content/article?article=2
	-> 14f0553caa5c796650ec82256e3f111ae2f20020a4b9029f135a01610932054e
	/foo/bar/barf/some/cms/content/article?article=3
	-> 4d45b9544077921575c3c5a2a14c779bff6c4830d1fbafe4bd7e03e5dd93ca05

That should be random enough.

But the key-compression does introduce a risk of collisions, since
not even SHA256 can guarantee different outputs for all possible
inputs:  Try pushing all the possible 33-byte files through SHA256
and sooner or later you will get collisions.

The risk of collision is very small however, and I can all but
promise you, that you will be fully offset in fame and money for
any inconvenience a collision might cause, because you will
be the first person to find a SHA256 collision.

Poul-Henning, 2012-01-03
