..
	Copyright (c) 2015-2016 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _phk_ssl_again:

=============
SSL revisited
=============

Four years ago, I wrote a rant about why Varnish has no SSL support
(:ref:`phk_ssl`) and the upcoming 4.1 release is good excuse to
revisit that issue.

A SSL/TLS library
~~~~~~~~~~~~~~~~~

In 2011 I criticized OpenSSL's source-code as being a nightmare,
and as much as I Hate To Say I Told You So, I Told You So:  See also
"HeartBleed".

The good news is that HeartBleed made people realize that FOSS
maintainers also have mortgages and hungry kids.

Various initiatives have been launched to make prevent critical
infrastructure software from being maintained Sunday evening between
11 and 12PM by a sleep-deprived and overworked parent, worried about
about being able to pay the bills come the next month.

We're not there yet, but it's certainly getting better.

However, implementing TLS and SSL is still insanely complex, and
thanks to Edward Snowden's whistle-blowing, we have very good reasons
to believe that didn't happen by accident.

The issue of finding a good TLS/SSL implementation is still the
same and I still don't see one I would want my name associated with.

OpenBSD's LibreSSL is certainly a step in a right direction, but
time will show if it is viable in the long run -- they do have
a tendency to be -- "SQUIRREL!!" -- distracted.

Handling Certificates
~~~~~~~~~~~~~~~~~~~~~

I still don't see a way to do that.  The Varnish worker-process is not
built to compartmentalize bits at a cryptographic level and making it
do that would be a non-trivial undertaking.

But there is new loop-hole here.
One night, waiting for my flight home in Oslo airport, I went though
the entire TLS/SSL handshake process to see if there were anything
one could do, and I realized that you can actually terminate TLS/SSL
without holding the certificate, provided you can ask some process
which does to do a tiny bit of work.

The next morning `CloudFlare announced the very same thing`_:

.. _CloudFlare announced the very same thing: https://blog.cloudflare.com/keyless-ssl-the-nitty-gritty-technical-details/

This could conceivably be a way to terminate TLS/SSL in the Varnish-worker
process, while keeping the most valuable crypto-bits away from it.

But it's still a bad idea
~~~~~~~~~~~~~~~~~~~~~~~~~

As I write this, the news that `apps with 350 million downloads`_ in total
are (still) vulnerable to some SSL/TLS Man-In-The-Middle attack is doing the
rounds.

.. _apps with 350 million downloads: http://arstechnica.com/security/2015/04/27/android-apps-still-suffer-game-over-https-defects-7-months-later/

Code is hard, crypto code is double-plus-hard, if not double-squared-hard,
and the world really don't need another piece of code that does an
half-assed job at cryptography.

If I, or somebody else, were to implement SSL/TLS in Varnish, it would
talk at least half a year to bring the code to a point where I would be
willing to show it to the world.

Until I get my time-machine working, that half year would be taken
away of other Varnish development, so the result had better be worth
it: If it isn't, we have just increased the total attack-surface
and bug-probability for no better reason than "me too!".

When I look at something like Willy Tarreau's `HAProxy`_ I have a
hard time to see any significant opportunity for improvement.

.. _HAProxy: http://www.haproxy.org/


Conclusion
~~~~~~~~~~

No, Varnish still won't add SSL/TLS support.

Instead in Varnish 4.1 we have added support for Willys `PROXY`_
protocol which makes it possible to communicate the extra details
from a SSL-terminating proxy, such as `HAProxy`_, to Varnish.

.. _PROXY: http://www.haproxy.org/download/1.5/doc/proxy-protocol.txt

From a security point of view, this is also much better solution
than having SSL/TLS integrated in Varnish.

When (not if!) the SSL/TLS proxy you picked is compromised by a
possibly planted software bug, you can pick another one to replace
it, without loosing all the benefits of Varnish.

That idea is called the "Software Tools Principle", it's a very old
idea, but it is still one of the best we have.


Political PostScript
~~~~~~~~~~~~~~~~~~~~

I realize that the above is a pretty strange stance to take in the
current "SSL Everywhere" political climate.

I'm not too thrilled about the "SSL Everywhere" idea, for a large
number of reasons.

The most obvious example is that you don't want to bog down your
country's civil defence agency with SSL/TLS protocol negotiations,
if their website is being deluged by people trying to survive a
natural disaster.

The next big issue is that there are people who do not have a right
to privacy.  In many countries this includes children, prisoners,
stock-traders, flight-controllers, first responders and so on.

SSL Everywhere will force institutions to either block any internet
connectivity or impose Man-in-The-Middle proxies to comply with
legal requirements of logging and inspection.  A clear step in
the wrong direction in my view.

But one of the biggest problem I have with SSL Everywhere is that
it gives privacy to the actors I think deserve it the least.

Again and again shady behaviour of big transnational, and therefore
law-less, companies have been exposed by security researchers (or
just interested lay-people) who ran tcpdump. snort or similar traffic
capture programs and saw what went on.

Remember all the different kind of "magic cookies" used to track
users across the web, against their wish and against laws and regulations ?

Pretty much all of those were exposed with trivial packet traces.

With SSL Everywhere, these actors get much more privacy to invade
the privacy of every human being with an internet connection, because
it takes a lot more skill to look into a SSL connection than a
plaintext HTTP connection.

"Sunshine is said to be the best of disinfectants" wrote supreme
court justice Brandeis, SSL Everywhere puts all traffic in the shade.

Poul-Henning, 2015-04-28
