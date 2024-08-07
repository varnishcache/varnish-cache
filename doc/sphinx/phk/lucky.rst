..
	Copyright (c) 2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _phk_lucky:

===================
Do you feel lucky ?
===================

Whenever a corporate cotton-mouth says anything, their lawyers
always make them add a footnote to the effect that *"Past performance
is not predictive of future results."* so they do not get sued for
being insufficiently clairvoyant.

The lawyers are wrong.

Past performance *is* predictive of future results:  That is how
we determine the odds.

Just never forget that the roll of the dice is still pure luck.

Or as author Neil Gaiman said it in
`a commencement speech in 2012 <https://www.youtube.com/watch?v=2OwRUyZMKwI>`_:

 *»Often you will discover that the harder you work,
 and the more wisely you work, the luckier you get.
 But there is luck, and it helps.«*

Approximately four million real websites use Varnish now and the
number seems to grow by half a million sites per year, as it has
been doing for the last 8 years.

That took a lot of luck, and wisdom was probably also involved, but
mostly it was lot of hard work by a lot of people.

Wisdom is a tricky thing, the hypothesized "older—wiser" correlation
is still in clinical testing and in the meantime Neil Gaiman suggests:

 *»So be wise, because the world needs more wisdom, and if you
 cannot be wise, pretend to be someone who is wise, and then just
 behave like they would.«*

Works for me.

----
2018
----

Despite sucking in pretty much any other aspect, 2018 was a good
year for Open Source Software in general and Varnish Cache in
particular.

People have finally started to understand that Free does not mean
Gratis, and that quality software takes time and effort.

From to the dot-com generation reinventing reproducible builds (like
we had then in the 1980'ies) to the EU setting up a pot
of `850M€ bug-bounties
<https://juliareda.eu/2018/12/eu-fossa-bug-bounties/>`_ [#f1]_,
things are moving in the right direction with respect to software
quality.

In 2018 the Varnish Cache project had settled into our "March and
September 15th" release strategy, and released 6.0 in March and 6.1
in September, as promised, and the next release will be out in ten
weeks.

We also had no security issues, and we have managed to keep the
number of open issues and bug reports down.

Writing it like that makes it sound boring, but with four million
web sites depending on Varnish, boring is good thing.

No news is indeed good news.

---------------
2019 and HTTP/3
---------------

The Next Big Thing in our world seems like it will be HTTP/3 ("The
protocol formerly known as QUIC"), and I suspect it will drive
much of our work in 2019.

It is far too early to say anything about if, when or how, but I
do spend a lot of time with pencil and paper, pretending to be
somebody who is good at designing secure and efficient software.

Around the time of the 2019-03-15 release we will gather for a VDD
(Varnish Developer Day), and the big topic there will be HTTP/3,
and then we will know and be ready to say something more detailed.

I don't think it is realistic to roll out any kind of H3 support
in the September release, that release will probably only contain
a some of the necessary preparatory reorganization, so expect to
run production on 6.0 LTS for a while.

---------------------
Varnish Moral License
---------------------

I want to thank the companies who have paid for a `Varnish
Moral License <http://phk.freebsd.dk/VML/index.html>`_:

* Fastly

* Uplex

* Varnish Software

* Section.io

* Globo

The VML funding is why Varnish Cache is not on EU's hit-list and
why another half million websites who started using Varnish in
2018 will not regret it.

Much appreciated!

-------
ENOLUCK
-------

For me 2018 ended on a sour note, when my dear friend `Jacob Sparre
Andersen <http://www.jacob-sparre.dk/>`_ died from cancer a week
before christmas.

Society as such knows how to deal with deaths, and all sorts of
procedures and rules kick in, to tie the loose ends up, respectfully
and properly.

The Internet is not there yet, people on the Internet have only
just started dying, and there are not yet any automatic routines
or generally perceived procedures for informing the people and
communities who should know, or for tying up the loose ends, accounts,
repositories and memberships on the Internet.

But deaths happen, and I can tell you from personal experience that
few things feel more awful, than having sent an email to somebody,
to receive the reply from their heartbroken spouse, that you are
many months too late [#f2]_.

Jacob was not a major persona on the Internet, but between doing a
lot of interesting stuff as a multi-discipline phd. in physics,
being a really good Ada programmer, a huge Lego enthusiast, an
incredibly helpful person *and* really *good* at helping, he had a
lot of friends in many corners of the Internet.

Jacob knew what was coming, and being his usual helpful self, he
used the last few weeks to make a list of who to tell online, where
things were stored, what the passwords were, and he even appointed
a close friend to be his "digital executor", who will help his widow
sort all these things out in the coming months.

When people die in our age-bracket, they usually do not get a few
weeks notice.  If Jacob had been hit by a bus, his widow would have
have been stuck in an almost impossible digital situation, starting
with the need to guess, well, pretty much everything, including
the passwords.

In honour of my helpful friend Jacob, and for the sake of your loved
ones, please sit down tonight, and write your own list of digital
who, what and where, including how to gain access to the necessary
passwords, and file it away in a way where it will be found, if
you run out of luck.

Good luck!

*phk*

.. rubric:: Footnotes

.. [#f1] I am not a big fan of bug-bounties, but I will grudgingly admit
   that wiser men than me, notably `Dan Geer
   <https://www.youtube.com/watch?v=nT-TGvYOBpI>`_, have proposed that
   tax-money be used to snatch the vulnerabilities up, before bad guys
   get hold of them, and they seem to have a point.

.. [#f2] And it does not feel any less awful if the loved ones
   left behind tries to fill the blanks by asking you how you knew
   each other and if you have any memories you could share with them.

