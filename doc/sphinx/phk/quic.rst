..
	Copyright (c) 2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _phk_quick_osi:

QUIC visions of OSI
===================

New York Times Style Magazine had an article last week
`about the Italian town Ivrea
<https://www.nytimes.com/2019/08/28/t-magazine/olivetti-typewriters-ivrea-italy.html>`_,
which you have probably never heard about.

Neither had I, 30+ years ago, when I got sent there as part of a project
to migrate the European Parliament from OSI protocols to TCP/IP.

What ?  You thought OSI protocols were only a theory ?

Nothing could be further from the truth.

One of the major reasons we are being bothered by Indian "Microsoft
Support" all the time is that the global telephone network runs on
Signalling System Number 7 ("SS7") which is very much an OSI
protocol.

Your electricity meter very likely talks DLMS(/COSEM), which is also
an OSI protocol.

In both cases, it cost serious money to just get to read the relevant
standards, which is why they could persist in this madness
undisturbed for decades.

ITU-T finally saw the light a few years back, so now you can actually
Read `Q.700 <https://www.itu.int/ITU-T/recommendations/index.aspx?ser=Q>`_
if you do not belive me.

Anyway, back in Luxembourg in the tail end of the 1980'ies, the European
parliament ran OSI protocols, and it sucked, and the more I dug into "The
Red/Yellow/Blue Book"[#f1]_, there more obvious it was that these
protocols were totally unsuitable for use on a local area network.

We proposed to migrate the European Parliament to use TCP/IP, and
we did, which gave me a memorable year in Ivrea, but we could only
do so on the explicit condition, imposed by the European Commission,
that the parliament would migrate back, "…once the issues with the
OSI protocols were sorted out."

They never sorted them out, because the OSI protocols were designed
and built by people who only considered communication between different
buildings, cities, countries and continents, but not what happened
inside each individual building [#f2]_.

Having seen the title of this rant, you can probably already see where
I'm going with this, and you will be mostly right.

The good news is that IETF learned their lesson, so QUIC is not
being rammed through and rubber-stamped the way HTTP/2 was,
in fact, one could argue that IETF got their revenge by handing
QUIC over to their arc-nemesis:
`The Transport Area <https://tools.ietf.org/area/tsv/>`_.

I think that was a good thing, because pretty much all of my
predictions about H2 came true, from the lack of benefits to the
DoS exposure designed into it.

All those aliments came by because the people who pushed "H2 the
protocol previously known as SPDY" only considered the world from
the perspective of a huge company with geo-diverse datacenters for
whom packet loss is something that happens to other people and
congestion is solved by sending an email to Bandwidth Procurement.

But those concerns are precisely what the "dinosaurs" in the Transport
Area care about and have studied and worked on for decades, so there
is every reason to expect that QUIC will emerge from the Transport
Area much better than it went in.

While I was pretty certain that H2 would be a fizzle, I have a much
harder time seeing where QUIC will go.

On the positive side, QUIC is a much better protocol, and it looks
like the kind of protocol we need in an increasingly mobile InterNet
where IP numbers are an ephemeral property.  This is the carrot, and
it is a big and juicy one.

In the neutral area QUIC is not a simple protocol, it is a full
transport protocol, which means loss detection, retransmission,
congestion control and all that, but you do not get better than TCP
without solving the problems TCP solved, and those are real and
hard problems.

On the negative side, QUIC goes a long way to break through barriers
of authority, both by putting it on top of UDP to get it through
firewalls, but also by the very strong marriage to TLS1.3 which
dials privacy up to 11:  Everything but the first byte of a QUIC
packet is encrypted.

Authorities are not going to like that, and I can easily see more
authoritarian countries outright ban QUIC, and to make that ban
stick, they may even transition from "allowed if not banned" to
"banned if not allowed" firewalling.

Of couse QUIC would still be a thing if you are big enough to
negotiate with G7-sized governments, and I would not be surprised
if QUIC ends up being a feasible protocol only for companies which
can point at the "job creation" their data-centers provide.

The rest of us will have to wait and see where that leaves us.

QUIC and Varnish
----------------

I can say with certainty that writing a QUIC implementation
from scratch, including TLS 1.3 is out of the question, that
is simply not happening.

That leaves basically three options:

1) Pick up a TLS library, write our own QUIC

2) Pick up a QUIC library and the TLS library it uses.

3) Stick with "That belongs in a separate process in front of Varnish."

The precondition for linking an TLS library to Varnishd, is that
the private keys/certificates are still isolated in a different
address space, these days known as "KeyLess TLS".

The good news is that QUIC is designed to do precisely that [#f3]_ .
The bad news is that as far as I can tell, none of the available
QUIC implementations do it, at least not yet.

The actual selection of QUIC implementations we could adopt is very
short, and since I am not very inclined to make Go or Rust a
dependency for Varnish, it rapidly becomes even shorter.

Presently, The H2O projects `quicly <https://github.com/h2o/quicly>`_
would probably be the most obvious candidate for us, but even that
would be a lot of work, and there is a some room between where
they have set their code quality bar, and where we have put ours.

However, opting to write our own QUIC instead of adopting one
is a lot of work, not in the least for the necessary testing,
so all else being equal, adopting sounds more feasible.

With number three we abdicate the ability to be "first line" if
QUIC/H3 does become the new black, and it would be incumbent on us
to make sure we work as well as possible with those "front burner"
boxes using a richer PROXY protocol or maybe a "naked" QUIC,
to maintain functionality.

One argument for staying out of the fray is that our "No TLS in
Varnish" policy looks like it was the right decision.

While it is inconvenient for small sites to have to run two
processes, as soon as sites grow, the feedback changes to
appreciation for the decoupling for TLS from policy/caching,
and once sites get even bigger, or more GDPR exposed, the
ability to use diverse TLS offloaders is seen as a big benefit.

Finally, there is the little detail of testing:  Varnishtest,
which has its own `VTest project <https://github.com/vtest/VTest>`_
now, will need to learn about HTTP3, QUIC and possibly TLS also.

And of course, when we ask the Varnish users, they say *"Ohhh...
they all sound delicious, can we have the buffet ?"* :-)

*phk*


.. rubric:: Footnotes

.. [#f1] The ITU-U's standards were meant to come out in updated
	 printed volumes every four years, each "period" a different
	 color.

.. [#f2] Not, and I want to stress this, because they were stupid
         or ignorant, but it simply was not their job.  Many
         of them, like AT&T in USA, were legally banned from
	 the "computing" market.

.. [#f3] See around figure 2 in `the QUIC/TLS draft <https://quicwg.org/base-drafts/draft-ietf-quic-tls.html>`_.
