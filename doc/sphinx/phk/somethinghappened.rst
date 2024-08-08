..
	Copyright (c) 2017 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _phk_somethinghappened:

====================================================
Something (funny) happened on the way to 5.1.0^H1^H2
====================================================

Some time back we, or to be perfectly honest, I, decided that in
the future we would do two varnish releases per year, march 15th
and september 15th and damn the torpedoes.

The 5.1.X release was the first real test of this and it went a
little less smooth than I would have preferred, and it is pretty
much all my own fault.

As you may have heard, we're in the process of `building a new house
<https://ing.dk/blog/murvaerk-196778>`_ and that obviously takes a
lot of time on my part, most recently we have put up our "old" house
for sale etc. etc.

So I was distracted, and suddenly it was march 15th but come hell
or high water:  We cut the release.

... and almost immediately found that it had a total show-stopper
bug which would cause the worker process to restart too often.

Ok, fix that and roll 5.1.1

... and find another two, not quite as severe but still unacceptable
problems.

Deep breath, fix those, and a lot of HTTP/2 stuff reported by
simon & xcir, who kindly subject that part of the code to some
live traffic ...  and roll 5.1.2.

This one will stick I hope.   Next release will be September 15th.

... unless something truly horrible lurks in 5.1.2.

Success, Failure or Meh? (strike out the not applicable)
--------------------------------------------------------

Seen from a release engineering point of view we live a very
sheltered life in the Varnish Project.

Our code base is small, 120 thousand lines of code and we
wrote almost all of it ourselves, which means that we
control the quality standard throughout.

Thanks to our focus on code-quality, we have never had to
rush out a bug/security-fix in the full glare of the combined
scorn of Nanog, Hackernews, Reddit and Metasploit [#f2]_.

We also don't link against any huge "middleware" libraries, I think
the biggest ones are Ncurses and PCRE [#f1]_, both of which are
quite stable, and we don't depend on any obscure single-compiler
languages either.

So while rushing out point releases with short notice is pretty
routine for many other projects, it was a new experience for us,
and it reminded us of a couple of things we had sort of forgotten [#f3]_.

I am absolutely certain that if we had not had our "release
by calendar" policy in place, I would probably not have been
willing to sign of on a release until after all the
house-building-moving-finding-where-I-put-the-computer madness
is over late in summer, and then I would probably still insist
on delaying it for a month just to catch my bearings.

That would have held some
`pretty significant new code </docs/5.1/whats-new/changes-5.1.html>`_
from our users for another half year, for no particular reason.

So yeah, it was pretty embarrassing to have to amend our 5.1 release
twice in two weeks, but it did prove that the "release by calendar"
strategy is right for our project:  It forced us to get our s**t
together so users can benefit from the work we do in a timely
fashion.

And thanks to the heroic testing efforts of Simon and Xcir, you may
even be able to use the HTTP/2 support in 5.1.2 as a result.

Next time, by which I mean "September 15th 2017", we'll try to do better.

Poul-Henning, 2017-04-11

.. rubric:: Footnotes

.. [#f1] Ncurses is just shy of 120 thousand lines of code and
	 PCRE is 96 thousand lines but that is getting of lightly,
         compared to linking against any kind of GUI.

.. [#f2] The bugs which caused 5.1.1 and 5.1.2 "merely" resulted
	 in bad stability, they were not security issues.

.. [#f3] Always release from a branch, in case you need to release again.
