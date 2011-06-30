.. _phk_3.0:

==================================
Thoughts on the eve of Varnish 3.0
==================================

Five years ago, I was busy transforming my pile of random doddles
on 5mm squared paper into software, according to "git log" working
on the first stevedores.

In two weeks I will be attending the Varnish 3.0 release party in Oslo.

Sometimes I feel that development of Varnish takes for ever and
ever, and that it must be like watching paint dry for the users,
but 3 major releases in 5 years is actually not too shabby come to
think of it.

Varnish 3.0 "only" has two big new features, VMOD and GZIP, and a
host of smaller changes, which you will notice if they are new
features, and not notice if they are bug fixes.

GZIP will probably be most important to the ESI users, and I wonder
if all the time I spent fiddling bits in the middle of compressed data
pays off, or if the concept of patchwork-quilting GZIP files was
a bad idea from end to other.

VMODs on the other hand, was an instant success, because they make
it much easier for people to extend Varnish with new functionality,
and I know of several VMODs in the pipeline which will make it
possible to do all sorts of wonderful things from VCL.

All in all, I feel happy about the 3.0 release, and I hope the users
will too.

We are not finished of course, ideas and patches for Varnish 4.0
are already starting to pile up, and hopefully we can get that into
a sensible shape 18 months from now, late 2012-ish.

	"Life is what happens to you while you're busy making other plans"

said John Lennon, a famous murder victim from New York.

I feel a similar irony in the way Varnish happened to me:

My homepage is written in raw HTML using the vi(1) editor, runs on
a book-sized Soekris NET5501 computer, averages 50 hits a day with
an Alexa rank just north of the 3.5 million mark.  A normal server
with Varnish could deliver all traffic my webserver has ever
delivered, in less than a second.

But varnish-cache.org has Alexa rank around 30.000, "varnish cache"
shows a nice trend on Google and #varnish confuses the heck out of
teenage girls and wood workers on Twitter, so clearly I am doing
something right.

I still worry about the `The Fraud Police 
<http://www.theshadowbox.net/forum/index.php?topic=18041.0>`_ though,
"I have no idea what I'm doing, and I totally make shit up as I go
along." is a disturbingly precise summary of how I feel about my
work in Varnish.

The Varnish 3.0 release is therefore dedicated to all the kind
Varnish developers and users, who have tested, reported bugs,
suggested ideas and generally put up with me and my bumbling ways
for these past five years.

Much appreciated,

Poul-Henning, 2011-06-02
