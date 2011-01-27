.. _phk_ipv6suckage:

============
IPv6 Suckage
============

In my drawer full of cassette tapes, is a 6 tape collection published
by Carl Malamuds "Internet Talk Radio", the first and by far the
geekiest radio station on the internet.

The tapes are from 1994 and the topic is "IPng", the IPv4 replacement
that eventually became IPv6.  To say that I am a bit jaded about
IPv6 by now, is accusing the pope of being religious.

IPv4 addresses in numeric form, are written as 192.168.0.1 and to
not confuse IPv6 with IPv4, it was decided in RFC1884 that IPv6
would use colons and groups of 16 bits, and because 128 bits are a
lot of bits, the secret '::' trick was introduced, to supress all
the zero bits that we may not ever need anyway: 1080::8:800:200C:417A

Colon was chosen because it was already used in MAC/ethernet addresses
and did no damage there and it is not a troublesome metacharacter
in shells.  No worries.

Most protocols have a Well Known Service number, TELNET is 23, SSH
is 22 and HTTP is 80 so usually people will only have to care about
the IP number.

Except when they don't, for instance when they run more than one
webserver on the same machine.

No worries, says the power that controls what URLs look like, we
will just stick the port number after the IP# with a colon:

	http://192.168.0.1:8080/...

That obviously does not work with IPv6, so RFC3986 comes around and
says "darn, we didn't think of that" and puts the IPV6 address in
[...] giving us:

	http://[1080::8:800:200C:417A]:8080/

Remember that "harmless in shells" detail ?  Yeah, sorry about that.

Now, there are also a RFC sanctioned API for translating a socket
address into an ascii string, getnameinfo(), and if you tell it that
you want a numeric return, you get a numeric return, and you don't
even need to know if it is a IPv4 or IPv6 address in the first place.

But it returns the IP# in one buffer and the port number in another,
so if you want to format the sockaddr in the by RFC5952 recommended
way (the same as RFC3986), you need to inspect the version field
in the sockaddr to see if you should do

	"%s:%s", host, port

or

	"[%s]:%s", host, port

Careless standardization costs code, have I mentioned this before ?

Varnish reports socket addresses as two fields: IP space PORT,
now you know why.

Until next time,

Poul-Henning, 2010-08-24
