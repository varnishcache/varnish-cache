..
	Copyright (c) 2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _phk_legacy:

=========
Legacy-.*
=========

In the middle of an otherwise pleaseant conversation recently, the
other person suddenly burst out that *"Varnish was part of our
legacy software."*

That stung a bit.

But fair enough:  They have been running varnish since 2009 or so.

Neither Raymond's "New Hacker's Dictionary", nor the legacy publication
it tried to replace, Kelly-Bootle's "The Devils DP Dictionary", define
"legacy software".  The former because the collective "we" did not
bother with utter trivialities such as invoicing, the latter because
back then people didnt abandon software.

Tomorrow I will be sitting in a small hut in the middle of a field,
trying to figure out what somebody could possibly have been thinking
about, when 10 years ago they claimed to implement "V.42" while
also using their own HDLC frame layout.

"V.42" and "HDLC" are also a legacy at this point, but chances are
you have used it:  That was the hot way to do error-correction on
modems, when dialing into a BBS or ISP in the 1990ies.

I guess I should say "legacy-modems" ?

Big-endianness, storing the bytes the sensible way for hex-dumps, is
rapidly becoming legacy, as the final old HP and SUN irons are
finally become eWaste.

Objectively there is no difference of merit between little-endian
and big-endian, the most successful computers architectures of all
time picked equally one or the other, and the consolidation towards
little-endian is driven more by *"It's actually not that important"*
than by anything else.

But we still have a bit of code which cares about endianness
in Varnish, in particular in the imported `zlib` code.

For a while I ran a CI client on a WLAN access point with a
big-endian MIPS-processor.  But with only 128MB RAM the spurious
error rate caused too much noise.

Nothing has been proclaimed "Legacy" more often and with more force,
than the IBM mainframe, but they are still around, keeping the books
balanced, as they have for half a century.

And because they were born with variable length data types, IBM
mainframes are big-endian, and because we in Varnish care about
portability, you can also run Varnish Cache on your IBM mainframe:
Thanks to Ingvar, there are "s390x" architecture Varnish
packages if you need them.

So I reached out to IBM's FOSS-outreach people and asked if we could
borrow a cup of mainframe to run a CI-client, and before I knew it,
the Varnish Cache Project had access to a virtual s390x machine somewhere
in IBM's cloud.

For once "In the Cloud" *literally* means "On somebody's mainframe" :-)

I'm not up to date on IBM Mainframe technology, the last one I used
was a 3090 in 1989, so I have no idea how much performance
IBM has allocated to us, on what kind of hardware, or what it might
cost.

But it runs a full CI iteration on Varnish Cache in 3 minutes flat,
making it one of the fastest CI-clients we have.

Thanks IBM!

Poul-Henning, 2021-05-24
