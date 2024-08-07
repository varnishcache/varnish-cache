.. _phk_cheri_1:

How Varnish met CHERI 1/N
=========================

I should warn you up front that Robert Watson has been my friend
for a couple of decades, so I am by no means a neutral observer.

But Robert is one of the smartest people I know, and when he first
explained his ideas for hardware capabilities, I sold all my shares
in sliced bread the very next morning.

Roberts ideas grew to become CHERI, and if you have not heard about
it yet, you should read up on it, now:

https://www.cl.cam.ac.uk/research/security/ctsrd/cheri/

The core idea in CHERI is that pointers are not integers, which
means that you cannot randomly make up or modify pointers to point
at random things anymore, whatever your intentions might be.

From a security point of view, this circumscribes several large
classes of commonly used attack-vectors, and Microsoft Research
found that CHERI stopped a full 43% of all the vulnerabilities they
saw in 2019:

https://msrc-blog.microsoft.com/2022/01/20/an_armful_of_cheris/

(Yes, we can pause while you sell all your shares in sliced bread.)

I have been eagerly waiting to see how my own Varnish HTTP Cache
Software would fare under CHERI, because one of my goals with the
Varnish software, was to turn the quality dial up to 11 to see if
it made any real-life difference.

Robert has graciously lent me a shell-account on one of his shiny
new MORELLO machines, which rock an ARM64 prototype CPU with CHERI
capabilites.

In this sequence of rants I will sing the saga of "How Varnish meets
CHERI" - as it happens.

My hope is that it will inspire and help other software projects
to take the CHERI-plunge, and to help convince ARM that "MORELLO"
should become a permanent part of the ARM architecture.

A very thin layer of Varnish
----------------------------

For those of you not familiar with Varnish, you will need to know:

* Varnish is an afterburner cache to HTTP webservers, written in
  C and it clocks in around 100KLOC.

* Around 20% of all webtraffic in the world pass through a Varnish instance.

* You configure Varnish with a domain-specific programming
  language called VCL, which is translated to C source code,
  compiled to a shared library, dlopen(3)'ed and executed.

* Varnish runs as two processes, a "manager" and a "child".
  The child process does not ``exec(2)`` after the ``fork(2)``.

* The source code is written in a very paranoid style, around 10%
  of all lines are asserts, and there are other layers of paranoia on
  top of that, for instance we always check that ``close(2)`` returns zero.

* We have 900+ test cases, which exercise 90%+ of our source lines.

* In 16 years, we have had just a single "Oh Shit!" security issue.

I still hate Autocrap tools
---------------------------

Autocrap is a hack on a hack on a hack which really ruins software
portability, but it is the "industry standard" so we use it also
for Varnish, no matter how much I hate it.

See: https://dl.acm.org/doi/abs/10.1145/2346916.2349257

Because a lot of software does not work in CHERI mode, there are two
kinds of packages for CheriBSD:  Regular and Cheri.

See: https://ctsrd-cheri.github.io/cheribsd-getting-started/packages/index.html

Autocrap does not grok that some packages end up in ``/usr/local`` and
some in ``/usr/local64``, so the first thing I had to do, was to explain
this::

	export LIBTOOLIZE=/usr/local64/bin/libtoolize
	export PCRE2_LIBS=`/usr/local/bin/pcre2-config --libs8`
	export PCRE2_CFLAGS=`/usr/local/bin/pcre2-config --cflags`
	${SRCDIR}/configure \
		[the usual options]

Things you just can't do with CHERI
-----------------------------------

A long long time ago, I wrote a "persistent storage" module for
Varnish, and in order to not rewrite too much of the rest of
the source code, the architecture ended up files ``mmap(2)``'ed to
a consistent address, containing data structures with pointers.

The advent of memory space layout randomization as a bandaid for
buffer-overflows (dont get me started!), made that architecture
unworkable, and for close to a decade this "stevedore" has been
named ``deprecated_persistent``.

We still keep it around, because it helps us test the internal APIs,
but it is obviously not going to work with CHERI so::
	
	${SRCDIR}/configure \
		--without-persistent-storage \
		[the usual options]

Dont panic (quite as detailed)
------------------------------

Varnish has a built in panic-handler which dumps a lot of
valuable information, so people dont need to send us 1TB
coredumps.

Part of the information dumped is a backtrace produced with
``libunwind``, which is not available in a CHERI version (yet),
so::

	${SRCDIR}/configure \
		--without-unwind \
		[the usual options]

[u]intptr_t is or isn't ?
-------------------------

The two typedefs ``uintptr_t`` and ``intptr_t`` are big enough to
hold a pointer so that you can write "portable" code which
does the kind of integer-pointer-mis-math which
CHERI prevents you from doing.

In theory we should not have any ``[u]intptr_t`` in Varnish,
since one of our quality policies is to never convert an integer
to a pointer.

But there are a couple of places where we have used them
for "private" struct members, instead of unions.

Those become the first stumbling block::

   vsm.c:601:15: error: shift count >= width of type
        vd->serial = VSM_PRIV_LOW(vd->serial + 1);
                     ^~~~~~~~~~~~~~~~~~~~~~~~~~~~

The confusing message is because In CHERI, ``[u]intptr_t``, like pointers,
are 16 bytes wide but in this case the integer-view is used as a bit-map.

For now, I change them to ``uint64_t``, and put them on the TODO list.

One of them is printed as part of the panic output::

    VSB_printf(vsb, "priv2 = %zd,\n", vfe->priv2);

But that doesn't work with the wider type, so::

    VSB_printf(vsb, "priv2 = %jd,\n", (intmax_t)vfe->priv2);

And with that Varnish compiles under CHERI, which we can check with::

    % file bin/varnishd/varnishd
    bin/varnishd/varnishd: […] CheriABI […]

First test-run
--------------

Just to see how bad it is, we run the main test-scripts::

    % cd bin/varnishtest
    % ./varnishtest -i -k -q tests/*.vtc
    […]
    38 tests failed, 33 tests skipped, 754 tests passed

That's not half bad…

*/phk*
