.. _whatsnew_relnote_5.0:

Varnish 5.0 Release Note
========================

This is the first Varnish release after the Varnish Project moved out
of Varnish Softwares basement, so to speak, and it shows.

But it is also our 10 year aniversary release, `Varnish 1.0 was
released`_ on September 20th 2006.

That also means that we have been doing this for 10 years
without any bad security holes.

So yeah… 5.0 is not what we had hoped it would be, but we are as
proud as one can possibly be anyway.

We have put the technical stuff in two separate documents:

* :ref:`whatsnew_changes_5.0`

* :ref:`whatsnew_upgrading_5.0`


The obligatory thank-you speech
-------------------------------

This release of Varnish Cache is brought to you by the generous
support and donations of money and manpower from four companies:

* Fastly

* Varnish Software

* UPLEX

* The company which prefers to simply be known as "ADJS"

Without them, this release, and for that matter all the previous
ones, would not have happened.

Even though they are by and large employees of the very
same companies, the developers who have contributed to
Varnish in a major way in this release also deserve thanks:

* Martin - HTTP/2 HPACK header compression code, stevedore API, VSL

* Niels & Geoff - Shard backend director, ban-lurker improvements

* Guillame - HTTP/2 support for varnishtest

* Dridi - Backend temperatures etc.

* Federico - Too many fixes and ideas to count

* Lasse - Our tireless release-manager

* Devon - Performance insights and critical review.

* The rest of the V-S crew, for too many things to mention.

We need more money
------------------

Until now Varnish Software has done a lot of work for the Varnish
Cache project, but for totally valid reasons, they are scaling that
back and the project either needs to pick up the slack or drop some
of those activities.

It is important that people understand that Free and Open Source
Software isn't the same as gratis software:  Somebody has to pay
the developers mortgages and student loans.

A very large part of the Varnish development is funded through the
`Varnish Moral License`_, which enables Poul-Henning Kamp to have
Varnish as his primary job, but right now he is underfunded to the
tune of €2000-3000 per month.

Please consider if your company makes enough money using Varnish
Cache, to spare some money, or employee-hours for its future
maintenance and development.


We also need more manpower
--------------------------

First and foremost, we could really use a Postmaster to look after
our mailman mailing lists, including the increasingly arcane art
of anti-spam techniques and invocations.

We also need to work more on our documentation, it is in bad need
of one or more writers which can actually write text rather than
code.

We could also use more qualified content for our new project homepage,
so a webmaster is on our shopping list as well.

Finally, we can always use C-developers, we have more ideas than
we have coders, and since we have very high standards for quality
things take time to write.

*phk*


.. _Varnish Moral License: http://phk.freebsd.dk/VML

.. _Varnish 1.0 was released: https://sourceforge.net/p/varnish/news/2006/09/varnish-10-released/
