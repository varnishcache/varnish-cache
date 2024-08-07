..
	Copyright (c) 2016 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _phk_10goingon50:

========================
Varnish - 10 going on 50
========================

Ten years ago, Dag-Erling and I were busy hashing out the big lines
of Varnish.

Hashing out had started on a blackboard at University of Basel
during the `EuroBSDcon 2005 <http://2005.eurobsdcon.org/>`_ conference,
and had continued in email and IRC ever since.

At some point in February 2006 Dag-Erling laid down the foundations of
our Subversion and source tree.

The earliest fragment which have survived the conversion to Git is
subversion commit number 9::

    commit 523166ad2dd3a65e3987f13bc54f571f98453976
    Author: Dag Erling Smørgrav <des@des.no>
    Date:   Wed Feb 22 14:31:39 2006 +0000

        Additional subdivisions.

We consider this the official birth-certificate of the Varnish Cache
FOSS project, and therefore we will celebrate the 10 year birthday
of Varnish in a couple of weeks.

We're not sure exactly how and where we will celebrate this, but
follow Twitter user `@varnishcache <https://twitter.com/varnishcache>`_
if you want don't want to miss the partying.

--------
VCLOCC1
--------

One part of the celebration, somehow, sometime, will be the "VCL
Obfuscated Code Contest #1" in the same spirit as the `International
Obfuscated C Code Contest <http://www.ioccc.org/>`_.

True aficionados of Obfuscated Code will also appreciate this
amazing `Obfuscated PL/1 <http://www.multicians.org/proc-proc.html>`_.

The official VCLOCC1 contest rules are simple:

* VCL code must work with Varnish 4.1.1
* As many Varnishd instances as you'd like.
* No inline-C allowed
* Any VMOD you want is OK
* You get to choose the request(s) to send to Varnishd
* If you need backends, they must be simulated by varnishd (4.1.1) instances.
* *We* get to publish the winning entry on the Varnish project home-page.

The *only* thing which counts is the amazing/funny/brilliant
VCL code *you* write and what it does.  VMODs and backends are just
scaffolding which the judges will ignore.

We will announce the submission deadline one month ahead of time, but
you are more than welcome to start already now.

--------
Releases
--------

Our 10 year anniversary was a good excuse to take stock and look at
the way we work, and changes are and will be happening.

Like any respectable FOSS project, the Varnish project has never been
accused, or guilty, of releasing on the promised date.

Not even close.

With 4.1 not even close to close.

Having been around that block a couple of times, (*cough* FreeBSD 5.0 *cough*)
I think I know why and I have decided to put a stop to it.

Come hell or high water [#f1]_, Varnish 5.0 will be released September
15th 2016.

And the next big release, whatever we call it, will be middle of
March 2017, and until we change our mind, you can trust a major
release of Varnish to happen every six months.

Minor releases, typically bugfixes, will be released as need arise,
and these should just be installable with no configuration changes.

Sounds wonderful, doesn't it ?  Now you can plan your upgrades.

But nothing comes free:  Until we are near September, we won't be able
to tell you what Varnish 5 contains.

We have plans and ideas for what *should* be there, and we will work
to reach those milestones, but we will not hold the release for "just this
one more feature" if they are not ready.

If it is in on September 15th, it will be in the release, if not, it wont.

And since the next release is guaranteed to come six months later,
it's not a catastrophe to miss the deadline.

So what's the problem and why is this draconian solution better ?

Usually, when FOSS projects start, they are started by "devops",
Varnish certainly did:  Dag-Erling ran a couple of sites
with Varnish, as did Kristian, and obviously Anders and Audun of
VG did as well, so finding out if you improved or broke things
during development didn't take long.

But as a project grows, people gravitate from "devops" to "dev",
and suddenly we have to ask somebody else to "please test -trunk"
and these people have their own calendars, and are not sure why
they should test, or even if they should test, much less what they
should be looking for while they test, because they have not been
part of the development process.

In all honesty, going from Varnish1 to Varnish4 the amount of
real-life testing our releases have received *before* being released
has consistently dropped [#f2]_.

So we're moving the testing on the other side of the release date,
because the people who *can* live-test Varnish prefer to have a
release to test.

We'll run all the tests we can in our development environments and
we'll beg and cajole people with real sites into testing also, but
we won't wait for weeks and months for it to happen, like we did
with the 4.1 release.

All this obviously changes the dynamics of the project, and it we
find out it is a disaster, we'll change our mind.

But until then:  Two major releases a year, as clock-work, mid-September
and mid-March.

----------------
Moving to github
----------------

We're also moving the project to github.  We're trying to find out
a good way to preserve the old Trac contents, and once we've
figured that out, we'll pull the handle on the transition.

Trac is starting to creak in the joints and in particular we're
sick and tired of defending it against spammers.  Moving to github
makes that Somebody Elses Problem.

We also want to overhaul the project home-page and try to get
a/the wiki working better.

We'll keep you posted about all this when and as it happens.

--------------------------------------------
We were hip before it was hip to be hipsters
--------------------------------------------

Moving to github also means moving into a different culture.

GitHub's statistics are neat, but whenever you start to measure
something, it becomes a parameter for optimization and competition,
and there are people out there who compete on github statistics.

In one instance the "game" is simply to submit changes, no matter
how trivial, to as many different projects as you can manage in
order to claim that you "contribute to a lot of FOSS projects".

There is a similar culture of "trophy hunting" amongst so-called
"security-researchers" - who has most CVE's to their name?  It
doesn't seem to matter to them how vacuous the charge or how
theoretical the "vulnerability" is, a CVE is a CVE to them.

I don't want to play that game.

If you are a contributor to Varnish, you should already have the
nice blue T-shirt and the mug to prove it.  (Thanks Varnish-Software!)

If you merely stumble over a spelling mistake, you merely
stumbled over a spelling mistake, and we will happily
correct it, and put your name in the commit message.

But it takes a lot more that fixing a spelling mistake to
become recognized as "a Varnish contributor".

Yeah, we're old and boring.

Speaking of which...

----------------------------
Where does 50 come into it ?
----------------------------

On January 20th I celebrated my 50 year birthday, and this was a
much more serious affair than I had anticipated:  For the first
time in my life I have received a basket with wine and flowers on
my birthday.

I also received books and music from certain Varnish users,
much appreciated guys!

Despite numerically growing older I will insist, until the day I
die, that I'm a man of my best age.

That doesn't mean I'm not changing.

To be honest, being middle-aged sucks.

Your body starts creaking and you get frustrated seeing people make
mistakes you warned them against.

But growing older also absolutely rulez, because your age allows
you to appreciate that you live in a fantastic future with a lot
of amazing changes - even if it will take a long time before
progress goes too far.

There does seem to be increasing tendency to want the kids off your
lawn, but I think I can control that.

But if not I hereby give them permission to steal my apples and
yell back at me, because I've seen a lot of men, in particular in
the technical world, grow into bitter old men who preface every
utterance with "As *I* already said *MANY* years ago...", totally
oblivious to how different the world has become, how wrong their
diagnosis is and how utterly useless their advice is.

I don't want to end up like that.

From now on my basic assumption is that I'm an old ass who is part
of the problem, and that being part of the solution is something I
have to work hard for, rather than the other way around.

In my case, the two primary physiological symptoms of middle age is
that after 5-6 hours my eyes tire from focusing on the monitor and
that my mental context-switching for big contexts is slower than
it used to be.

A couple of years ago I started taking "eye-breaks" after lunch.
Get away from the screen, preferably outside where I could rest my
eyes on stuff further away than 40cm, then later in the day
come back and continue hacking.

Going forward, this pattern will become more pronounced.  The amount
of hours I work will be the same, but I will be splitting the workday
into two halves.

You can expect me to be at my keyboard morning (08-12-ish EU time)
and evening (20-24-ish EU time) but I may be doing other stuff,
away from the keyboard and screen, during the afternoon.

Starting this year I have also changed my calendar.

Rather than working on various projects and for various customers
in increments of half days, I'm lumping things together in bigger
units of days and weeks.

Anybody who knows anything about process scheduling can see that
this will increase throughput at the cost of latency.

The major latency impact is that one of the middle weeks of each
month I will not be doing Varnish.  On the other hand, all
the weeks I do work on Varnish will now be full weeks.

And with those small adjustments, the Varnish project and I are
ready to tackle the next ten years.

Let me conclude with a big THANK YOU! to all Contributors and Users
of Varnish, for making the first 10 years more amazing than I ever
thought FOSS development could be.

Much Appreciated!

*phk*

.. rubric:: Footnotes

.. [#f1] I've always wondered about that expression.  Is the assumption that
   if *both* hell *and* high water arrives at the same time they will cancel
   out ?

.. [#f2] I've seriously considered if I should start a porn-site, just to
   test Varnish, but the WAF of that idea was well below zero.
