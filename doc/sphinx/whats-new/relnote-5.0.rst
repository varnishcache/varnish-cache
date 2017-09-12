.. _whatsnew_relnote_5.0:

Varnish 5.0 Release Note
========================

This is the first Varnish release after the Varnish Project moved out
of Varnish Software's basement, so to speak, and it shows.

But it is also our 10 year anniversary release, `Varnish 1.0 was
released`_ on September 20th 2006.

That also means that we have been doing this for 10 years
without any bad security holes.

So yeah... 5.0 is not entirely what we had hoped it would be, but we
are as proud as one can possibly be anyway.

To keep this release note short(er), we have put the purely technical
stuff in two separate documents:

* :ref:`whatsnew_changes_5.0`

* :ref:`whatsnew_upgrading_5.0`

How to get Varnish 5.0
----------------------

`Source download <https://varnish-cache.org/_downloads/varnish-5.0.0.tgz>`_

Packages for mainstream operating systems should appear in as
soon as they trickle through the machinery.


Reasons to upgrade to Varnish 5.0
---------------------------------

The separate VCL/VCL labels feature can probably help you untangle
your VCL code if it has become too complex.  Upgrading from 4.1
to get that feature should be a no-brainer.

The HTTP/2 code is not mature enough for production, and if you
want to start to play with H2, you should not upgrade to 5.0,
but rather track -trunk from github and help us find all the bugs
before the next release.

The Shard director is new in the tree, but it has a lot of live
hours out of tree.  Upgrading from 4.1 to 5.0 to get that should
also be a no-brainer.

We have also fixed at lot of minor bugs, and improved many details
here and there, See :ref:`whatsnew_upgrading_5.0` for more of this.


Reasons not to upgrade to Varnish 5.0
-------------------------------------

None that we know of at this time.

Only in very special cases should you need to modify your VCL.


Next release
------------

Next release is scheduled for March 15th 2017, and will most
likely be Varnish 5.1.


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

Even though they are all employees of those very
same companies, these developers merit personal praise:

* Martin - HTTP/2 HPACK header compression code, stevedore API, VSL

* Nils & Geoff - Shard backend director, ban-lurker improvements

* Guillame - HTTP/2 support for varnishtest

* Dridi - Backend temperatures etc.

* Federico - Too many fixes and ideas to count

* Lasse - Our tireless release-manager

* Devon - Performance insights and critical review.

* The rest of the V-S crew - Too many things to list.


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
tune of EUR 2000-3000 per month.

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

The best way to get involved is to just jump in and do stuff that
needs done.

Here is the `Varnish Cache github page <https://github.com/varnishcache/varnish-cache>`_.

And here is the `Varnish Projects homepage on github <https://github.com/varnishcache/varnish-cache>`_.

Welcome on board!

*phk*


.. _Varnish Moral License: http://phk.freebsd.dk/VML

.. _Varnish 1.0 was released: https://sourceforge.net/p/varnish/news/2006/09/varnish-10-released/
