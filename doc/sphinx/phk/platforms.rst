.. _phk_platforms:

=================
Picking platforms
=================

Whenever you write Open Source Software, you have to make a choice of
what platforms you are going to support.

Generally you want to make your program as portable as possible and
cover as many platforms, distros and weird computers as possible.

But making your program run on everything is hard work very hard work.

For instance, did you know that:

	sizeof(void*) != sizeof(void * const)

is legal in a ISO-C compliant environment ?

Varnish `runs on a Nokia N900 <http://hellarvik.com/node/66>`_
but I am not going to go out of my way to make sure that is always
the case.

To make sense for Varnish, a platform has to be able to deliver,
both in terms of performance, but also in terms of the APIs we
use to get that performance.

In the FreeBSD project where I grew up, we ended up instituting
platform-tiers, in an effort to document which platforms we
cared about and which we did love quite as much.

If we did the same for Varnish, the result would look something like:

A - Platforms we care about
---------------------------

We care about these platforms because our users use them and
because they deliver a lot of bang for the buck with Varnish.

These platforms are in our "tinderbox" tests, we use them ourselves
and they pass all regression tests all the time. 
Platform specific bug reports gets acted on.

*FreeBSD*

*Linux*

Obviously you can forget about running Varnish on your
`WRT54G <http://en.wikipedia.org/wiki/Linksys_WRT54G_series>`_
but if you have a real computer, you can expect Varnish to work
"ok or better" on any distro that has a package available.

B - Platforms we try not to break
---------------------------------

We try not to break these platforms, because they basically work,
possibly with some footnotes or minor limitations, and they have
an active userbase.

We may or may not test on these platforms on a regular basis,
or we may rely on contributors to alert us to problems.
Platform specific bug reports without patches will likely live a quiet life.

*Mac OS/X*

*Solaris-decendants* (Oracle Solaris, OmniOS, Joyent SmartOS)

Mac OS/X is regarded as a developer platform, not as a production
platform.

Solaris-decendants are regarded as a production platform.

NetBSD, AIX and HP-UX are conceivably candidates for this level, but
so far I have not heard much, if any, user interest.

C - Platforms we tolerate
-------------------------

We tolerate any other platform, as long as the burden of doing
so is proportional to the benefit to the Varnish community.

Do not file bug reports specific to these platforms without attaching
a patch that solves the problem, we will just close it.

For now, anything else goes here, certainly the N900 and the WRT54G.

I'm afraid I have to put OpenBSD here for now, it is seriously
behind on socket APIs and working around those issues is just not
worth the effort.

If people send us a small non-intrusive patches that makes Varnish
run on these platforms, we'll take it.

If they send us patches that reorganizes everything, hurts code
readability, quality or just generally do not satisfy our taste,
they get told that thanks, but no thanks.

Is that it ?  Abandon all hope etc. ?
-------------------------------------

These tiers are not static, if for some reason Varnish suddenly
becomes a mandatory accessory to some technically sensible platform,
(zOS anyone ?) that platform will get upgraded.  If the pessimists
are right about Oracles intentions, Solaris may get demoted.


Until next time,

Poul-Henning, 2010-08-03
Edited Nils, 2014-03-18 with Poul-Hennings concent
