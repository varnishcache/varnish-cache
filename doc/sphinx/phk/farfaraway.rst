.. _phk_farfaraway:

=============
Far, far away
=============

I realize I'm showing my age when I admit that Slades 1974 hit `"Far
Far Away" <https://www.youtube.com/watch?v=6gqCCAb8xbw>`_ was one
of the first rock-ballads I truly loved.  (In case you have never
heard of Slade or the 1970'ies British glam-rock, you may want to
protect your innocence and *not* click on that link.)

Some years back I got invited to a conference in New Zealand, and
that is "far far away" from Denmark.  So far away in fact, that I
downloaded the entire
`Bell Systems Technical Journal <https://archive.org/details/bstj-archives>`_
to my Kobo eReader in order to have something to do during the 24
hour air-traffic "experience".

BSTJ is good reading, for instance you learn that they invented
`Agile Programming <https://archive.org/stream/bstj62-7-2365#page/n21/mode/2up>`_
back in 1983, but failed to come up with a hip name.

Anyway, Internet Access in New Zealand is like time-travel back to
around Y2K or so, and when one of my time-nuts friends launched a
`Kickstarter project <https://www.kickstarter.com/projects/1575992013/kiwisdr-beaglebone-software-defined-radio-sdr-with>`_ it didn't take much before his residential connection folded.

As it happens, I am in the process of setting up the new Varnish-Cache.org
project server just now, generously sponsored/donated by `RootBSD.com
<https://www.RootBSD.com>`_, so it was natural for me to offer to
help him out.

I don't need to explain varnishhist to this audience::


			|
			|
			||
			||
			||
			||
			||
			||
			||
			||
			||
			||
			||                            ##
		       |||                            ##
		       |||                         #  ## #
		      |||||                        #  #####
	+-------+-------+-------+-------+-------+-------+-------+-------+-------
	|1e-6   |1e-5   |1e-4   |1e-3   |1e-2   |1e-1   |1e0    |1e1    |1e2

Most of us who live in civilized places, tend to forget that the InterNet
is very unevenly distributed.

My ISP enabled IPv6 on the VDSL2+ line to my beach-house today,
some people have fiber, but in terms head-count, the majority of
the world has really horrible internet connections.

In some cases it is the last mile, for instance if you live out at some
remote fjord in Norway.

In other cases it is a mid-net bottle-neck, in the case of New
Zealand a shortage of transoceanic fiber cables [#f1]_ .

Caching is not a cure-all, it is far from a miracle cure, even thought it
might seem that way sometimes.

But as prophylactic for bandwidth troubles, it is second to none.

One of the goals of Varnish was that it should be easy to roll out
in a crisis situation, start it, repoint your DNS, suffer less,
tune it a little bit (usually: ignore cookies) and suffer a lot less.

Today was a good sanity-check for me, trying exactly that.

All in all it worked out pretty well, as the varnishhist above shows.


*phk*

.. [#f1] These `BSTJ articles about the first Atlantic phone cable
   <https://archive.org/details/bstj-archives?&and[]=bstj%20%201957-1-1>`_
   will give you an appreciation of why that is not a trivial problem
   to solve.


