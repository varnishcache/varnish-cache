..
	Copyright (c) 2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _phk_ip_address:

======================================
IP Addresses - A long expected problem
======================================

I'm old enough to remember `HOSTS.TXT` and the introduction of the DNS system.

Those were the days when you got a class B network by sending a
polite letter to California, getting a polite letter back, and then,
some months later, when
`RFC1166 INTERNET NUMBERS <https://tools.ietf.org/html/rfc1166>`_
arrived with in semi-annual packet of printed RFCs,
find out that letter had at typo and you had configured all of
the European Parliaments 1200 computers on 136.172/16 instead of
136.173/16.

But things were not simpler, if anything they were far more complex,
because TCP/IP was not, as today, the only protocol that mattered.

In addition to TCP/IP, there were IBM's SNA, Digitals DecNet,
ApolloRing, Banyan/VINES, Novell NetWare, X.21, X.25, X.75, and the
whole CCITT-OSI-"Intelligent Network" telecom disaster that never
got off the ground.

This is why DNS packets have a `class` field which can be set to
`Hesiod` or `CHAOS` in addition to `the Internet`:  The idea was
that all the different protocols would get a number each, and we
would have "The One Directory To Rule Them All".

Largely because of this, a new and "protocol agnostic" lookup
functions were designed: `getaddrinfo(3)` and `getnameinfo(3)`,
but of course for IP they were supposed to be backwards compatible
because there were *thousands* of users out there already.

This is why `telnet 80.1440` tries to connect to `80.0.5.160`,
why `ping 0x7f000001` becomes `127.0.0.1` and `0127.0.0.1`
becomes `87.0.0.1`.

If you read the manual page for `getaddrinfo(3)` you will find
that it does not tell you that, it merely says it
`conforms to IEEE Std 1001`.

But everybody knew what that was back in 1990, and nobody had firewalls
anyway because Cheswick & Bellowins book
`Firewalls and Internet Security <http://www.wilyhacker.com/>`_
was not published until 1994, so no worries ?

As is often the case with 'designed for the future' the `getaddrinfo(3)`
API instantly fossilized, hit by a freeze-ray in the 'the Unix™ wars'.

This is why, when IPv4 numbers started to look like a finite resource,
and the old A-, B- and C- class networks got dissolved into Classless
Inter-Domain Routing or "CIDR" netmasks of any random size, getaddrinfo(3)
did not grow to be able to translate "192.168.61/23" into something useful.

I believe there were also some lilliputian dispute about the fact that
`192.168.61` would return `192.168.0.61` to stay backwards compatible,
whereas `192.168.61/23` would return `192.168.61.0 + 255.255.254.0`.

Because of this, Varnish uses `getaddrinfo(3)` everywhere but one single
place:  Parsing of ACL specifications in VCL.  First we have to use our
own parser to check if it is a CIDR entry and if not we ask `getaddrinfo(3)`.

The reason for this rant, is that somebody noticed that `ping
0127.0.0.1` didn't go to `127.0.0.1` as they expected.

That has just become CVE-2021-29418 and CVE-2021-28918 and will
probably become a dozen more, once the CVE-trophy-hunters go to town.

All IP number strings enter Varnish from trusted points, either
as command line arguments (`-a`, `-b`, `-M` etc.),
in the VCL source (`backend`, `acl` etc.) or as PROXYv1 header
strings from the TLS-stripper in front of Varnish.

Of course, VCL allows you to do pretty much anything, including::

    if (std.ip(req.http.trustme) ~ important_acl) {
         ...
    }

If you do something like that, you may want to a) Consider the wisdom
of trusting IP#'s from strangers and b) Think about this "critical
netmask problem".

Otherwise, I do not expect this new "critical netmask problem" to
result in any source code changes in Varnish.

If and when the various UNIX-oid operating systems, and the smoking
remains of the "serious UNIX industry", (IEEE ?  The Austin Group
?  The Open Group ?  Whatever they are called these days) get their
act together, and renovate the `getaddrinfo(3)` API, Varnish will
automatically pick that up and use it.

Should they, in a flash of enlightenment, also make `getaddrinfo(3)`
useful for parsing these newfangled CIDR addresses we got in 1993,
I will be more than happy to ditch `vcc_acl_try_netnotation()` too.

Until next time,

Poul-Henning, 2021-03-30
