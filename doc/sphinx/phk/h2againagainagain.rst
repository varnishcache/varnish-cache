.. _phk_h2_again_again_again:

On the deck-chairs of HTTP/2
============================

Last week some people found out that the HTTP/2 protocol is not so
fantastic when it comes to Denial-of-Service attacks.

Funny that.

The DoS vulnerability "found" last week, and proudly declared
"zero-day" in the heroic sagas of supreme DevOps-ness, can be found
in section 6.5.2, page 38 bottom, of RFC7540, from 2015:

.. code-block:: none

   SETTINGS_MAX_CONCURRENT_STREAMS (0x3):  Indicates the maximum number
      of concurrent streams that the sender will allow.  This limit is
      directional: it applies to the number of streams that the sender
      permits the receiver to create.  Initially, there is no limit to
      this value.  It is recommended that this value be no smaller than
      100, so as to not unnecessarily limit parallelism.

Long before that RFC was made official, some of us warned about
``there is no limit``, but that would not be a problem because "We
have enough CPU's for that", we were told, mainly by the people
from the very large company who pushed H2 down our throat.

Overall HTTP/2 has been a solid disappointment, at least if you
believed any of the lofty promises and hype used to market it.

Yes, we got fewer TCP connections, which is nice when there are
only sixty-something thousand port numbers available, and yes we
got some parallelism per TCP connection.

Of course the price for that parallelism is that a dropped
packet no longer delays a single resource:  Now it delays *everything*.

During the ratification period, this got the nickname
"Head-Of-Line-Blocking" or just "HoL-blocking", but that would also
not be a problem because "the ISPs would be forced to (finally) fix
that" - said some guy with a Google Fiber connection to his Silly
Valley home.

But we also got, as people "discovered" last week, a lot more
sensitive to DoS attacks - by design - because the entire point
of H2 was to get as much work done as soon as possible.

Then there is the entire section in the H2 RFC about "Stream
Priority", intended to reduce the risk of people reading the
words in the article until all the "monetization" were in place.

As far as I know, nobody ever got that to do anything useful,
unless they had somebody standing on the toes 24*7, pointing their
nose at the ever-moving moon, while reciting Chaucer from memory.
I think all browsers just ignore it now.

Oh… and "PUSH":  The against-all-principles reverse primitive, so
the server could tell the browser, that come hell or high water,
you will need these advertisements right away.

Nobody got that working either.

But my all time personal favorite is this one:

The static HPACK compression table contains entries for the headers
``proxy-authentication`` and ``proxy-authorization``, which by
definition can never appear in a H2 connection.  But they were in
some random dataset somebody used to construct the table, and "it
was far too late to change that now", because we were in a hurry
to get H2 deployed.

We have a saying in Danish: »Hastværk er lastværk«, which roughly
translates to »Hurry and be Sorry«, and well, yeah...

(If you want to read what I thought at the time, this is a draft
I never completed because I realized that H2 was just going to be a
rubber-stamping exercise: https://phk.freebsd.dk/sagas/httpbis/)

Once it became clear that H2 would happen, DoS vulnerabilities and
all, I dialed down my complaining about the DoS problems, partly
because I saw no reason to actively tell the script-kiddies and
criminals what to do, but mostly because clearly nobody was listening:
"It's a bypass.  You've got to build bypasses."

Somewhat reluctantly we implemented H2 in Varnish, because people
told us they really, really would need this, it was going to be a
checkbox item for the C-team once they read about it in the WSJ,
and all the cool kids already had it &c &c.

We didn't do a bad job of it, but we could probably have done it
even better, if we had felt it would worth it.

But given that the ink on H2 was barely dry before QUIC was being
launched to replace it, and given that DoS vulnerabilities were
literally written into the standard, we figured that H2 was unlikely
to overtake the hot plate and the deep water as Inventions of The
Century, and economized our resources accordingly.

I am pleasantly surprised that it took the bad guys this long to
weaponize H2.  Yes, there are 100 pages in the main RFC and neither
Hemmingway or Prachett were on the team, but eight years ?  Of
course there is no knowing how long time it has been a secret weapon
in some country's arsenal of "cyber weapons".

But now that the bad guys have found it, and weaponized it, what do we do ?

My advice:

Unless you have solid numbers to show that H2 is truly improving
things for you and your clients, you should just turn it off.
Remember to also remove it from the ALPN string in hitch or whatever
TLS off-loader you use.

If for some reason you cannot turn H2 off, we are implementing some
parameters which can help mitigate the DoS attacks, and we will
roll new releases to bring those to you.

But other than that, please do not expect us to spend a lot of time
rearranging the deck-chairs of HTTP/2.

*/phk*
