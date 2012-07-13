.. _phk_http20_lack_of_interest:

======================================
Why HTTP/2.0 does not seem interesting
======================================

This is the email I sent to the IETF HTTP Working Group::


	From:    Poul-Henning Kamp <phk@phk.freebsd.dk>
	Subject: HTTP/2 Expression of luke-warm interest: Varnish
	To:      HTTP Working Group <ietf-http-wg@w3.org>
	Message-Id: <41677.1342136900@critter.freebsd.dk>
	Date:    Thu, 12 Jul 2012 23:48:20 GMT


This is Varnish' response to the call for expression of interest
in HTTP/2[1].

Varnish
-------

Presently Varnish[2] only implements a subset of HTTP/1.1 consistent
with its hybrid/dual "http-server" / "http-proxy" role.

I cannot at this point say much about what Varnish will or will
not implement protocol wise in the future.

Our general policy is to only add protocols if we can do a better
job than the alternative, which is why we have not implemented HTTPS
for instance.

Should the outcome of the HTTP/2.0 effort result in a protocol which
gains traction, Varnish will probably implement it, but we are
unlikely to become an early implementation, given the current
proposals at the table.


Why I'm not impressed
---------------------

I have read all, and participated in one, of the three proposals
presently on the table.

Overall, I find all three proposals are focused on solving yesteryears
problems, rather than on creating a protocol that stands a chance
to last us the next 20 years.

Each proposal comes out of a particular "camp" and therefore
all seem to suffer a certain amount from tunnel-vision.

It is my considered opinion that none of the proposals have what
it will take to replace HTTP/1.1 in practice.


What if they made a new protocol, and nobody used it ?
------------------------------------------------------

We have learned, painfully, that an IPv6 which is only marginally
better than IPv4 and which offers no tangible benefit for the people
who have the cost/trouble of the upgrade, does not penetrate the
network on its own, and barely even on goverments mandate.

We have also learned that a protocol which delivers the goods can
replace all competition in virtually no time.

See for instance how SSH replaced TELNET, REXEC, RSH, SUPDUP, and
to a large extent KERBEROS, in a matter of a few years.

Or I might add, how HTTP replaced GOPHER[3].

HTTP/1.1 is arguably in the top-five most used protocols, after
IP, TCP, UDP and, sadly, ICMP, and therefore coming up with a
replacement should be approached humbly.


Beating HTTP/1.1
----------------

Fortunately, there are many ways to improve over HTTP/1.1, which
lacks support for several widely used features, and sports many
trouble-causing weeds, both of which are ripe for HTTP/2.0 to pounce
on.

Most notably HTTP/1.1 lacks a working session/endpoint-identity
facility, a shortcoming which people have pasted over with the
ill-conceived Cookie hack.

Cookies are, as the EU commision correctly noted, fundamentally
flawed, because they store potentially sensitive information on
whatever computer the user happens to use, and as a result of various
abuses and incompetences, EU felt compelled to legislate a "notice
and announce" policy for HTTP-cookies.

But it doesn't stop there:  The information stored in cookies have
potentialiiy very high value for the HTTP server, and because the
server has no control over the integrity of the storage, we are now
seing cookies being crypto-signed, to prevent forgeries.

The term "bass ackwards" comes to mind.

Cookies are also one of the main wasters of bandwidth, disabling
caching by default, sending lots of cookies were they are are not
needed, which made many sites register separate domains for image
content, to "save" bandwidth by avoiding cookies.

The term "not really helping" also comes to mind.

In my view, HTTP/2.0 should kill Cookies as a concept, and replace
it with a session/identity facility, which makes it easier to
do things right with HTTP/2.0 than with HTTP/1.1.

Being able to be "automatically in compliance" by using HTTP/2.0
no matter how big dick-heads your advertisers are or how incompetent
your web-developers are, would be a big selling point for HTTP/2.0
over HTTP/1.1.

However, as I read them, none of the three proposals try to address,
much less remedy, this situation, nor for that matter any of the
many other issues or troubles with HTTP/1.x.

What's even worse, they are all additive proposals, which add a
new layer of complexity without removing any of the old complexity
from the protocol.

My conclusion is that HTTP/2.0 is really just a grandiose name for
HTTP/1.2:  An attempt to smoothe out some sharp corners, to save a
bit of bandwidth, but not get anywhere near all the architectural
problems of HTTP/1.1 and to preserve faithfully its heritage of
badly thought out sedimentary hacks.

And therefore, I don't see much chance that the current crop of
HTTP/2.0 proposals will fare significantly better than IPv6 with
respect to adoption.


HTTP Routers
------------

One particular hot-spot in the HTTP world these days is the
"load-balancer" or as I prefer to call it, the "HTTP router".

These boxes sit at the DNS resolved IP numbers and distributes
client requests to a farm of HTTP servers, based on simple criteria
such as "Host:", URI patterns and/or server availability, sometimes
with an added twist of geo-location[4].

HTTP routers see very high traffic densities, the highest traffic
densities, because they are the focal point of DoS mitigation, flash
mobs and special event traffic spikes.

In the time frame where HTTP/2.0 will become standardized, HTTP
routers will routinely deal with 40Gbit/s traffic and people will
start to arcitect for 1Tbit/s traffic.

HTTP routers are usually only interested in a small part of the
HTTP request and barely in the response at all, usually only the
status code.

The demands for bandwidth efficiency has made makers of these devices
take many unwarranted shortcuts, for instance assuming that requests
always start on a packet boundary, "nulling out" HTTP headers by
changing the first character and so on.

Whatever HTTP/2.0 becomes, I strongly urge IETF and the WG to
formally recognize the role of HTTP routers, and to actively design
the protocol to make life easier for HTTP routers, so that they can
fulfill their job, while being standards compliant.

The need for HTTP routers does not disappear just because HTTPS is
employed, and serious thought should be turned to the question of
mixing HTTP and HTTPS traffic on the same TCP connection, while
allowing a HTTP router on the server side to correctly distribute
requests to different servers.

One simple way to gain a lot of benefit for little cost in this
area, would be to assign "flow-labels" which each are restricted
to one particular Host: header, allowing HTTP routers to only examine
the first request on each flow.


SPDY
----

SPDY has come a long way, and has served as a very worthwhile proof
of concept prototype, to document that there are gains to be had.

But as Frederick P. Brooks admonishes us:  Always throw the prototype
away and start over, because you will throw it away eventually, and
doing so early saves time and effort.

Overall, I find the design approach taken in SPDY deeply flawed.

For instance identifying the standardized HTTP headers, by a 4-byte
length and textual name, and then applying a deflate compressor to
save bandwidth is totally at odds with the job of HTTP routers which
need to quickly extract the Host: header in order to route the
traffic, preferably without committing extensive resources to each
request.

It is also not at all clear if the built-in dictionary is well
researched or just happens to work well for some subset of present
day websites, and at the very least some kind of versioning of this
dictionary should be incorporated.

It is still unclear for me if or how SPDY can be used on TCP port
80 or if it will need a WKS allocation of its own, which would open
a ton of issues with firewalling, filtering and proxying during
deployment.

(This is one of the things which makes it hard to avoid the feeling
that SPDY really wants to do away with all the "middle-men")

With my security-analyst hat on, I see a lot of DoS potential in
the SPDY protocol, many ways in which the client can make the server
expend resources, and foresee a lot of complexity in implementing
the server side to mitigate and deflect malicious traffic.

Server Push breaks the HTTP transaction model, and opens a pile of
cans of security and privacy issues, which whould not be sneaked
in during the design of a transport-encoding for HTTP/1+ traffic,
but rather be standardized as an independent and well analysed
extension to HTTP in general.


HTTP Speed+Mobility
-------------------

Is really just SPDY with WebSockets underneath.

I'm really not sure I see any benefit to that, execept that the
encoding chosen is marginally more efficient to implement in
hardware than SPDY.

I have not understood why it has "mobility" in the name, a word
which only makes an appearance in the ID as part of the name.

If the use of the word "mobility" only refers only to bandwidth
usage, I would call its use borderline-deceptive.

If it covers session stability across IP# changes for mobile
devices, I have missed it in my reading.


draft-tarreau-httpbis-network-friendly-00
-----------------------------------------

I have participated a little bit in this draft initially, but it
uses a number of concepts which I think are very problematic for
high performance (as in 1Tbit/s) implementations, for instance
variant-size length fields etc.

I do think the proposal is much better than the other two, taking
a much more fundamental view of the task, and if for no other reason,
because it takes an approach to bandwidth-saving based on enumeration
and repeat markers, rather than throwing everything after deflate
and hope for a miracle.

I think this protocol is the best basis to start from, but like
the other two, it has a long way to go, before it can truly 
earn the name HTTP/2.0.


Conclusion
----------

Overall, I don't see any of the three proposals offer anything that
will make the majority of web-sites go "Ohh we've been waiting for
that!"

Bigger sites will be entised by small bandwidth savings, but the
majority of the HTTP users will see scant or no net positive benefit
if one or more of these three proposals were to become HTTP/2.0

Considering how sketchy the HTTP/1.1 interop is described it is hard
to estimate how much trouble (as in: "Why doesn't this website work ?")
their deployment will cause, nor is it entirely clear to what extent
the experience with SPDY is representative of a wider deployment or
only of 'flying under the radar' with respect to people with an
interest in intercepting HTTP traffic.

Given the role of HTTP/1.1 in the net, I fear that the current rush
to push out a HTTP/2.0 by purely additive means is badly misguided,
and approaching a critical mass which will delay or prevent adoption
on its own.

At the end of the day, a HTTP request or a HTTP response is just
some metadata and an optional chunk of bytes as body, and if it
already takes 700 pages to standardize that, and HTTP/2.0 will add
another 100 pages to it, we're clearly doing something wrong.

I think it would be far better to start from scratch, look at what
HTTP/2.0 should actually do, and then design a simple, efficient
and future proof protocol to do just that, and leave behind all
the aggregations of badly thought out hacks of HTTP/1.1.

But to the extent that the WG produces a HTTP/2.0 protocol which
people will start to use, the Varnish project will be interested.


Poul-Henning Kamp

Author of Varnish


[1] http://trac.tools.ietf.org/wg/httpbis/trac/wiki/Http2CfI

[2] http://varnish-cache.org/

[3] Yes, I'm that old.

[4] Which is really a transport level job, but it was left out of IPv6
    along with other useful features, to not delay adoption[5].

[5] No, I'm not kidding.

