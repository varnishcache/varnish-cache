..
	Copyright (c) 2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _who_is:

Who is ... ?
============

Not quite `Twurp's Peerage <https://wiki.lspace.org/mediawiki/Twurp%27s_Peerage>`_ but a Who's Who of the Varnish Cache project.

Anders Berg
~~~~~~~~~~~

Blame Anders!  He is the one who got the crazy idea that the world
needed another HTTP proxy server software, and convinced his employer,
the norwegian newspaper `Verdens Gang <http://www.vg.no>`_ to pay for the
first version to be developed.

Here is an interview with Anders about `how it all began
<http://info.varnish-software.com/blog/celebrating-10-years-of-varnish-cache-qa-with-the-man-behind-the-idea>`_

Dag-Erling Smørgrav
~~~~~~~~~~~~~~~~~~~

DES was working at Redpill-Linpro, a norwegian UNIX/Open Source company
when Anders floated his idea for a "forward HTTP cache", he lured PHK
into joining, was one of the original developers (doing Linux), project
manager and release engineer for the first three years of the project,
and forced us to adopt a non-US-ASCII charset from the start.

Poul-Henning Kamp
~~~~~~~~~~~~~~~~~

PHK, as he's usually known, has written most of the code and come up with
most of the crazy ideas in Varnish, and yet he still has trouble
remembering what 'REST', 'CORS' and 'ALPN' means, and he flunked
'CSS for dummies' because he was never a webmaster or webdeveloper.
He does have 30+ years of experience in systems programming, and
that seems useful too.

PHK's `random outbursts </docs/trunk/phk/index.html>`_ has their own
section in the Varnish documentation.

Per Buer
~~~~~~~~

Per also worked at Redpill-Linpro, and at some point when the
impedance mismatch between Linpros "normal way of doing things" and
the potential of Varnish became to steep, he convinced the company
to spin off `Varnish Software <https://varnish-software.com/>`_
with himself at the helm.

Do a git blame on the Varnish documentation and you will be surprised
to see how much he cares about it. Very few people notice this.

Ingvar Hagelund
~~~~~~~~~~~~~~~

Ingvar works as Team Leader (read very skilled sysadmin) at Redpill-Linpro,
but his passion is reading books and blogging about it, as well as RPM
packaging. So every Fedora and EPEL (read RedHat and CentOS) Varnish user
out there owe him a thanks or two. Once in a while, he also trawls the
internet checking for the rate of Varnish adoption among top web sites.

Stig Sandbeck Mathisen
~~~~~~~~~~~~~~~~~~~~~~

Stig works at Redpill-Linpro and is the guy in charge of packaging Varnish
for Debian, which means Ubuntu users owe him a thanks also. Besides this,
he maintains VCL-mode for emacs and is generally a nice and helpful guy.


Tollef Fog Heen
~~~~~~~~~~~~~~~

Tollef was product owner and responsible for Varnish while working
for Redpill-Linpro. later tech lead at Varnish Software and held
the Varnish release manager helmet for a few years. His experience with
open source (Debian, Ubuntu and many others) brought sanity to the
project in ways that are hard to measure or describe.

Kristian Lyngstøl
~~~~~~~~~~~~~~~~~

Kristian was the first Varnish SuperUser, and he quite literally
wrote the book, while giving Varnish courses for Redpill-Linpro,
and he pushed boundaries where no boundaries had been pushed before
which caused a lot of improvements and "aha!" moments in Varnish.

Artur Bergman
~~~~~~~~~~~~~

Artur ran Wikias webservers and CDN when he discovered Varnish and
eagerly adopted it, causing many bugreports, suggestions, patches
and improvements.  At some point, he pivoted Wikias CDN into the
Varnish based startup-CDN named `Fastly <http://www.fastly.com/>`_

Kacper Wysocki
~~~~~~~~~~~~~~

Kacper was probably the first VCL long program writer. Combine this with
an interest in security and a job at Redpill-Linpro and he turned
quickly into the author of security.vcl and, later, the Varnish Security
Firewall. He does not have any commits in Varnish and still has managed
to drive quite a few changes into the project. Similarly, he has no idea
or has even thought about asking for it, and still is being added here
He maintains the VCL grammar in BNF notation, which is an unexploited
gold mine.

Nils Goroll
~~~~~~~~~~~

aka 'slink' is the founder of `UPLEX <http://uplex.de/>`_, a five-head
tech / consultancy company with negative to zero marketing (applied
for entry into the "Earth's worst company homepage" competition). He
fell in love with Varnish when he migrated Germany's Verdens Gang
counterpart over a weekend in March 2009 and, since then, has
experienced countless moments of pure joy and happiness when, after
struggling for hours, he finally understood another piece of
beautiful, ingenious Varnish code.

Nils' primary focus are his clients and their projects. He tries to
make those improvements to Varnish which matter to them.

Martin Blix Grydeland
~~~~~~~~~~~~~~~~~~~~~

Martin was the first full-time member of the C-team at Varnish Software.
He is the main responsible for the amazing revamp of the logging
facilities and utilities in the 4.0 cycle and later the storage
rework. Besides that he fixes lots of bugs, knows varnishtest better
than most, writes vmods and is the Varnish Cache Plus architect.

Lasse Karstensen
~~~~~~~~~~~~~~~~

Lasse is the current release manager and stable version maintainer of
Varnish Cache. When not doing that, he maintains build infrastructure
and runs the Varnish Software C developer team in Oslo.

Geoff Simmons
~~~~~~~~~~~~~

Geoff started working at UPLEX in 2010 and soon learned to love
Varnish as much as slink does. Since then he's been contributing code
to the project, writing up various VMODs (mostly about regular
expressions, blobs, backends and directors), developing standalone
applications for logging that use Martin's VSL API, and adding custom
patches to Varnish for various customer needs. He spends most of his
days in customer projects as "the Varnish guy" on the operations
teams.
