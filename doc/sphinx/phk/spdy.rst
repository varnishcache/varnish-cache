.. _phk_spdy:

===================================
What SPDY did to my summer vacation
===================================

It's dawning on me that I'm sort of the hipster of hipsters, in the sense
that I tend to do things far before other people do, but totally fail to
communicate what's going on out there in the future, and thus by the
time the "real hipsters" catch up, I'm already somewhere different and
more interesting.

My one lucky break was the `bikeshed email <http://bikeshed.org/>`_ where
I actually did sit down and compose some of my thoughts, thus firmly
sticking a stick in the ground as one of the first to seriously think
about how you organize open source collaborations.

I mention this because what I am going to write probably seems very
unimportant for most of the Varnish users right now, but down the road,
three, five or maybe even ten years ahead, I think it will become important.

Feel free to not read it until then.

The evolution of Varnish
------------------------

When we started out, seven years ago, our only and entire goal was to build
a server-side cache better than squid.  That we did.

Since then we have added stuff to Varnish (ESI:includes, gzip support,
VMODS) and I'm staring at streaming and conditional backend fetches right
now.

Varnish is a bit more than a web-cache now, but it is still, basically,
a layer of polish you put in front of your webserver to get it to
look and work better.

Google's experiments with SPDY have forced a HTTP/2.0 effort into motion,
but if past performance is any indication, that is not something we have
to really worry about for a number of years. The IETF WG has still to
manage to "clarify" RFC2616 which defines HTTP/1.1, and to say there
is anything even remotely resembling consensus behind SPDY would be a
downright lie.

RFC2616 is from June 1999, which, to me, means that we should look at
2035 when we design HTTP/2.0, and predicting things is well known to
be hard, in particular with respect to the future.

So what's a Varnish architect to do?

What I did this summer vacation, was to think a lot about how Varnish
can be architected to cope with the kind of changes SPDY and maybe HTTP/2.0
drag in:  Pipelining, multiplexing, etc., without committing us to one
particular path of science fiction about life in 2035.

Profound insights often sound incredibly simplistic, bordering
trivial, until you consider the full ramifications.  The implementation
of "Do Not Kill" in current law is surprisingly voluminous.  (If
you don't think so, you probably forgot to #include the Vienna
Treaty and the convention about chemical and biological weapons.)

So my insight about Varnish, that it has to become a socket-wrench-like
toolchest for doing things with HTTP traffic, will probably elicit a lot
of "duh!" reactions, until people, including me, understand the 
ramifications more fully.

Things you cannot do with Varnish today
---------------------------------------

As much as Varnish can be bent, tweaked and coaxed into doing today,
there are things you cannot do, or at least things which are very
hard and very inefficient to do with Varnish.

For instance we consider "a transaction" something that starts with
a request from a client, and involves zero or more backend fetches
of finite sized data elements.

That is not how the future looks.

For instance one of the things SPDY has tried out is "server push",
where you fetch index.html and the webserver says "you'll also want
main.css and cat.gif then" and pushes those objects on the client,
to save the round-trip times wasted waiting for the client to ask
for them.

Today, something like that is impossible in Varnish, since objects
are independent and you can only look up one at a time.

I already can hear some of you amazing VCL wizards say "Well,
if you inline-C grab a refcount, then restart and ..." but let's
be honest, that's not how it should look.

You should be able to do something like::

	if (req.proto == "SPDY" && req.url ~ "index.html") {
		req.obj1 = lookup(backend1, "/main.css")
		if (req.obj1.status == 200) {
			sess.push(req.obj1, bla, bla, bla);
		}
		req.obj2 = lookup(backend1, "/cat.gif")
		if (req.obj1.status == 200) {
			sess.push(req.obj2, bla, bla, bla);
		}
	}

And doing that is not really *that* hard, I think.  We just need
to keep track of all the objects we instantiate and make sure they
disappear and die when nobody is using them any more.

A lot of the assumptions we made back in 2006 are no longer
valid under such an architecture, but those same assumptions are
what gives Varnish such astonishing performance, so just replacing
them with standard CS-textbook solutions like "garbage collection"
would make Varnish lose a lot of its lustre.

As some of you know, there is a lot of modularity hidden inside
Varnish but not quite released for public use in VCL. Much of what
is going to happen will be polishing up and documenting that
modularity and releasing it for you guys to have fun with, so it
is not like we are starting from scratch or anything.

But some of that modularity stands on foundations which are no longer
firm; for instance, the initiating request exists for the full duration of
a backend fetch.

Those will take some work to fix.

But, before you start to think I have a grand plan or even a clear-cut
road map, I'd better make it absolutely clear that is not the case:
I perceive some weird shapes in the fog of the future and I'll aim
in that direction and either they are the doorways I suspect
or they are trapdoors to tar-pits, time will show.

I'm going to be making a lot of changes and things that used to be
will no longer be as they used to be, but I think they will be better
in the long run, so please bear with me, if your favourite detail
of how Varnish works changes.

Varnish is not speedy, Varnish is fast!
---------------------------------------

As I said I'm not a fan of SPDY and I sincerely hope that no bit of
the current proposal survives unchallenged in whatever HTTP/2.0 standard
emerges down the road.

But I do want to thank the people behind that mess, not for the
mess, but for having provoked me to spend some summertime thinking
hard about what it is that I'm trying to do with Varnish and what
problem Varnish is here to solve.

This is going to be FUN!


Poul-Henning 2012-09-14

Author of Varnish

PS: See you at `VUG6 <https://www.varnish-cache.org/vug6>`_ where I plan
to talk more about this.

