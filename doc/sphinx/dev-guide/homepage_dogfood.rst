..
	Copyright (c) 2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of licens

.. _homepage_dogfood:

How our website works
=====================

The principle of eating your own dogfood is important for software
quality, that is how you experience what your users are dealing with,
and I am not the least ashamed to admit that several obvious improvements
have happened to Varnish as a result of running the project webserver.

But it is also important to externalize what you learn doing so, and
therefore I thought I would document here how the projects new "internal
IT" works.

Hardware
--------

Who cares?

Yes, we use some kind of hardware, but to be honest I don't know what
it is.

Our primary site runs on a `RootBSD 'Omega' <https://www.rootbsd.net/>`_
virtual server somewhere near CDG/Paris.

And as backup/integration/testing server we can use any server,
virtual or physical, as long as it has a internet connection and
contemporary performance, because the entire install is scripted
and under version control (more below).

Operating System
----------------

So, dogfood:  Obviously FreeBSD.

Apart from the obvious reason that I wrote a lot of FreeBSD and
can get world-class support by bugging my buddies about it, there
are two equally serious reasons for the Varnish Project to run on
FreeBSD:  Dogfood and jails.

Varnish Cache is not "software for Linux", it is software for any
competent UNIX-like operating system, and FreeBSD is our primary
"keep us honest about this" platform.

Jails
-----

You have probably heard about Docker and Containers, but FreeBSD
have had jails
`since I wrote them in 1998 <http://phk.freebsd.dk/sagas/jails/>`_
and they're a wonderful way to keep your server installation
sane.

We currently have three jails:

* Hitch - runs the `Hitch SSL proxy <https://hitch-tls.org/>`_

* Varnish - <a href="rimshot.mp3">You guessed it</a>

* Tools - backend webserver, currently `ACME Labs' thttpd <http://acme.com/software/thttpd/>`_

Script & Version Control All The Things
---------------------------------------

We have a git repos with shell scripts which create these jails
from scratch and also a script to configure the host machine
properly.

That means that the procedure to install a clone of the server
is, unabridged::

	# Install FreeBSD (if not already done by hosting)
	# Configure networking (if not already done by hosting)
	# Set the clock
	service ntpdate forcestart
	# Get git
	env ASSUME_ALWAYS_YES=yes pkg install git
	# Clone the private git repo
	git clone ssh://example.com/root/Admin
	# Edit the machines IP numbers in /etc/pf.conf
	# Configure the host
	sh build_host.sh |& tee _.bh
	# Build the jails
	foreach i (Tools Hitch Varnish)
		(cd $i ; sh build* |& tee _.bj)
	end

From bare hardware to ready system in 15-30 minutes.

It goes without saying that this git repos contains stuff
like ssh host keys, so it should *not* go on github.

Backups
-------

Right now there is nothing we absolutely have to backup, provided
we have an up to date copy of the Admin git repos.

In practice we want to retain history for our development tools
(VTEST, GCOV etc.) and I rsync those file of the server on a
regular basis.


The Homepage
------------

The homepage is built with `Sphinx <http://www.sphinx-doc.org/>`_
and lives in its own
`github project <https://github.com/varnishcache/homepage>`_ (Pull requests
are very welcome!)

We have taken snapshots of some of the old webproperties, Trac, the
Forum etc as static HTML copies.

Why on Earth...
---------------

It is a little bit tedious to get a setup like this going, whenever
you tweak some config file, you need to remember to pull the change
back out and put it in your Admin repos.

But that extra effort pays of so many times later.

You never have to wonder "who made that change and why" or even try
to remember what changes were needed in the first place.

For us as a project, it means, that all our sysadmin people
can build a clone of our infrastructure, if they have a copy of
our "Admin" git repos and access to github.

And when `FreeBSD 11 <https://www.youtube.com/watch?v=KOO5S4vxi0o>`_
comes out, or a new version of sphinx or something else, mucking
about with things until they work can be done at leisure without
guess work.  (We're actually at 12 now, but the joke is too good
to delete.)

For instance I just added the forum snapshot, by working out all
the kinks on one of my test-machines.

Once it was as I wanted it, I pushed the changes the live machine and then::

	varnishadm vcl.use backup
	# The 'backup' VCL does a "pass" of all traffic to my server
	cd Admin
	git pull
	cd Tools
	sh build_j_tools.sh |& tee _.bj
	varnishadm vcl.load foobar varnish-live.vcl
	varnishadm vcl.use foobar

For a few minutes our website was a bit slower (because of the
extra Paris-Denmark hop), but there was never any interruption.

And by doing it this way, I *know* it will work next time also.

2016-04-25 /phk

PS: All that buzz about "reproducible builds" ?  Yeah, not a new idea.
