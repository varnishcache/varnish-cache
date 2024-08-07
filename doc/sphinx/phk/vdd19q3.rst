..
	Copyright (c) 2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _vdd19q3:

Varnish Developer Day 2019Q3
============================

We try to bring the core Varnish People into the same room a
couple of times a year for "Varnish Developer Day" meetings,
and last week we did that at Varnish Software's Oslo offices.

Tuesday was a "Hackathon", where we lounged around and worked
concrete issues and ideas, Wednesday was the "formal" day where
we sat around a table and negotiated and made decisions.

The main issues this time were HTTP3, backends and project
organization, and here I will try to give a quick summary.

HTTP3 and QUIC
--------------

Everybody seemed to agree that we want H3 support, and after
Dag's quick overview of the protocol, the challenge of doing
that were evident.

We also agreed that having certificates and secret keys inside
the varnish worker process is still a no-go, so some variant
of "key-less" is called for.  Fortunately H3 is designed with
this in mind for performance reasons.

Getting from A-B is the hard part, and we may introduce a A'
pit-stop where we implement key-less TLS1.3 on HTTP1+2, and
possibly also a A'' pitstop to get TLS on backends.

Dag and PHK will try to plot a course for this.

Backends
--------

There are a lot of annoying details about backends we want to
do something about, from probes being near-magical to H1 to
getting a proper handle on the lifetime of dynamic backends.

Some concrete improvements came up during the hackathon and we will
be persuing those right away.

Fixing probing is probably a V7 thing, and we need to think
and prototype how we expose probing in VCL.

Bugwash
-------

We are getting more people involved on the other side of the Atlantic,
and we are moving the Monday afternoon bugwash from 13:00-14:00 EU
time to 15:00-15:30 EU time, so they do not have to get out of bed
so early.

We will also try to make the bugwash more productive, by having PHK
publish an "agenda" some hours beforehand, so people can prepare,
and instead shorten the bugwash to 30 minutes to keep the time
commitment the same.

Everybody is welcome to attend our bugwashs, on the IRC channel
#varnish-hacking on irc.linpro.no.

Project organization
--------------------

There has been some friction in the project this summer and we
have talked a lot about how to counter that.

A significant part of the problem is that too much of the project
business goes through me:  I am always the one nagging and no'ing
peoples pull requests and that makes both them and me unhappy.

We have drawn up a set of "rules of engagement" which will distribute
the workload more evenly, essentially assuring that somebody from
another organization will have looked at patches and pull requests
before me, both to move some of the "no-ing" away from me and also
to get people to pay more attention to each others work.

For this to work, everybody will have to spend a bit more time on
"project work", but everybody agreed to do that, so we think it can
fly.

These discussions also brought up another thing:

Retirement Notice
-----------------

One interesting feature of the IT industry, is that there are no
retirement parties, because the industry more or less got born in
the 1990'ies.

There was an IT industry before then, I was part of it for most of
a decade, and it did have retirement parties, because people had been
going at it since the 50ies.

One almost invariable part of the proceedings were the "Handling
Over Of The Listing", where the retiree ceremoniously handed over
a four inch thick Z-fold listing of "The XYZ Program" to the younger
person now assuming responsibility for its care, feeding & maintenance,
until his - or the program's - retirement.

If you do the math, you will find that I am now also getting into
my 50ies, and the prospect of retirement is migrating from "theoretical
event in distant future" to "I need to think about this."

On Tuesday the 20th of January 2026 I will be 60 years old, the
Varnish Cache project will be 20 years old, and I will be retired
from active project management in the Varnish Cache Project.

That is six½ years in the future, a full half the current age of
the project, and a long time in IT, but I want to reserve the date,
so that the project has plenty of time to figure out what they want
to do about it.

The VDD appointed Martin and Nils to own that issue.

*phk*
