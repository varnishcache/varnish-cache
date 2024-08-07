..
	Copyright (c) 2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _phk_patent:

A patently good idea
====================

When I was in USA, diplomas on the wall was very much a thing.

I don't think I fully reverse-engineered the protocol for which
diplomas would get hung and which would be filed away, apart from
the unbreakable rule that, like it or not, anything your company
handed out was mandatory on the office-wall, no matter how embarrasing.

Our paediatrician had diplomas for five or six steps of her education.

My favourite pizzeria had a diploma for "Authentic Italian Food"
from a US organization suffering from fuzzy territorial perception.

Co-workers had diplomas from their universities, OSHA, USAF, DoE,
CalTrans and who knows what.

But the gold-standard of diplomas, at least amongst the engineers,
was having a US Patent on the wall, even if it only ever made them
a single dollar in assignment fee.

I asked one of them about his patent and he answered wryly: *"It
proves to my boss and my mom that I had at least one unique idea
in my career."*

Personally I do not think the patent system does what people think
it does, ie: protect the small inventor from big companies, so I
have no patents to my name, and in fact no diplomas on my wall at
all.

But I still mentally carve a notch when I see one of my ideas
being validated in some form.

Containers and Zones are not jails, but they know, and I know, where
they got the basic idea from, and that is plenty of validation
for my ego.

Today is Store Bededag in Denmark, loosely translated "All Prayers
Day", by definition a friday and we, like many other danes, have
eloped to the beach-house for a long weekend.

But being self-employed I puttered around with VCC, the VCL compiler,
this morning, and as a result, you will soon be able to say::

	import vmod_with_impractically_long_name as v;

(You can thank Dridi for suggesting that)

My idea that Varnish would be configured in a Domain Specific
Language compiled to native code is obviously one of my better,
and about 10 years ago, that was becoming very obvious.

In Norway `Varnish Software <https://varnish-software.com>`_ were
being spun out of the Redpill-Linpro company.

Artur Bergman, one of the first Varnish Cache power users, who ran
Wikias content delivery and hit our project like a blast-oven with
ideas, patches, measurements, general good cheer and incredibly low
tolerance for bull-shit, started the `Fastly CDN <https://fastly.com>`_.

Prior to that, I had done a bit of soul-searching myself, wondering
if I should try to take Varnish and run with it?

In conventional economic theory, I would have patented the
VCL idea, and become as rich as the idea was good.

But in all probable worlds, that would only have meant that the
idea would be dead as a doornail, I would not have made any money
from it, it would never have helped improve the web, and I would
have wasted much more of my life in meetings than would be good for
anybody's health.

As if that wasn't enough, the very thought of having to hire somebody
scared me, but not nearly as much as the realization that if I built
a company with any number of employees, sooner or later I would
have to fire someone again.

Writing code? Yes.

Running a growing company? No.

The result of my soul-searching was this email to announce@ where
I took myself out of the game:

.. code-block:: text

	Subject: For the record: Varnish and Money
	From: Poul-Henning Kamp <phk@phk.freebsd.dk>
	To: varnish-announce@varnish-cache.org
	Date: Fri Nov 19 14:03:22 CET 2010

	Just so everybody know where I stand on this...

	Poul-Henning

	-----BEGIN PGP SIGNED MESSAGE-----
	Hash: SHA1


	Introduction
	- ------------

	As the main developer of the Varnish Software and the de-facto leader
	of the Varnish Open Source Project, it is my desire to see Varnish
	used and adopted as widely as possible.

	To the same ends, the founders of the Varnish Project chose the BSD
	license to facilitate commercial exploitation of Varnish in all
	forms, while protecting the independence of the Open Source Project.

	The BSD license is non-discriminatory, and makes no attempt to
	separate the good guys from the bad guys, and neither should it.

	The Varnish Project, as a community, is a different story.

	While the BSD license can guarantee that Varnish, as software, will
	always be available, a thriving Open Source Community takes a fair
	bit more effort to hold together.

	Nothing can rip apart an Open Source project faster than competing
	commercial interests playing dirty, and since Varnish has started
	to cause serious amounts of money to shift around, it is time to
	take this issue a bit more seriously.


	Non-competition pledge:
	- -----------------------

	My interest in Varnish is developing capable quality software, and
	making a living at the same time.

	In addition to Varnish, I have some long time good customers for
	whom I do various weird things with computers and software, and
	since they have stuck with me and paid my bills, I will stick with
	them and send them more bills.

	The Varnish Moral License (VML) was drawn up to provide a money-stream
	that can fund my Varnish-habit, and it was designed as an "arms-length"
	construction to prevent it from taking control of the projects
	direction.

	Therefore acquiring a VML does not mean that you get to tell me
	what to do, or in which order I should do it.  There is no "tit for
	tat" involved.  The only thing you get out of the VML, is that the
	next version of Varnish will be better than the one we have now.

	Therefore:

	 As long as I can keep my family fed, happy and warm this
	 way, I will not enter any other commercial activity related
	 to Varnish, and am more than happy to leave that field open
	 to everybody and anybody, who wants to try their hand.


	Fairness pledge:
	- ----------------

	As the de-facto leader of the Varnish community, I believe that
	the success or failure of open source rises and falls with the
	community which backs it up.

	In general, there is a tacit assumption, that you take something
	from the pot and you try put something back in the pot, each to his
	own means and abilities.

	And the pot has plenty that needs filling:  From answers to newbies
	questions, bug-reports, patches, documentation, advocacy, VML funding,
	hosting VUG meetings, writing articles for magazines, HOW-TO's for
	blogs and so on, so this is no onerous demand for anybody.

	But the BSD license allows you to not participate in or contribute
	to the community, and there are special times and circumstances
	where that is the right thing, or even the only thing you can do,
	and I recognize that.

	Therefore:

	 I will treat everybody, who do not contribute negatively to
	 the Varnish community, equally and fairly, and try to foster
	 cooperation and justly resolve conflicts to the best of my
	 abilities.


	Policy on Gifts:
	- ----------------

	People sometimes prefer to show their appreciation of Varnish by
	sending me gifts.

	I really love that

	But please understand, that any and gifts or other appreciations I
	may receive, from cartoons on my Amazon Wishlist, up to and including
	pre-owned tropical tax-shelter islands, with conveniently unlocked
	bank vaults filled with gold bars (one can always dream...), will
	all be received and interpreted the same way:  As tokens of
	appreciation for deeds already done, and encouragement to me to
	keep doing what is right and best for Varnish in the future.


	Poul-Henning Kamp

	Signed with my PGP-key, November 19th, 2010, Slagelse, Denmark.
	-----BEGIN PGP SIGNATURE-----
	Version: GnuPG v1.4.10 (FreeBSD)

	iEYEARECAAYFAkzmdRkACgkQlftZhnGqOJOJwwCffytQ5kGP+Grh2unpNIIw8G2R
	QcQAn18fGLT4Lx2ACBivtk5wEFy6fUcu
	=3V52
	-----END PGP SIGNATURE-----
	--
	Poul-Henning Kamp       | UNIX since Zilog Zeus 3.20
	phk@FreeBSD.ORG         | TCP/IP since RFC 956
	FreeBSD committer       | BSD since 4.3-tahoe
	Never attribute to malice what can adequately be explained by incompetence.

Today (20190517) Arturs `Fastly <https://fastly.com>`_, company
went public on the New York Stock Exchange, and went up from $16
to $24 in a matter of hours.  So-called "financial analysts" write
that as a consequence Fastly is now worth 2+ Billion Dollars.

I can say with 100% certainty and honesty that there is no way
I could *ever* have done that, that is entirely Arturs doing and
I know and admire how hard he worked to make it happen.

Congratulations to Artur and the Fastly Crew!

But I will steal some of Arturs thunder, and point to Fastlys IPO
as proof that at least once in my career, I had a unique idea worth
a billion dollars :-)

*phk*
