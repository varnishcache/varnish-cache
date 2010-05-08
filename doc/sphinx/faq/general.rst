%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
General questions
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

What is Varnish?
================

Varnish is a state-of-the-art, high-performance `web accelerator <http://en.wikipedia.org/wiki/Web_accelerator>`_. It uses the advanced features in Linux 2.6, FreeBSD 6/7 and Solaris 10 to achieve its high performance.

Some of the features include

* A modern design
* VCL - a very flexible configuration language
* Load balancing with health checking of backends
* Partial support for ESI
* URL rewriting
* Graceful handling of "dead" backends

Features to come (Experimental):

* Support for Ranged headers
* Support for persistent cache
	

Varnish is free software and is licenced under a modified BSD licence. Please read the introduction to get started with Varnish.


How...
======

... How much RAM/Disk do I need for Varnish ?

    That depends on pretty much everything.

    I think our best current guidance is that you go for a cost-effective
    RAM configuration, something like 1-16GB, and a SSD disk.

    Unless you positively know that you will need it, there is
    little point in spendng a fortune on a hand-sewn motherboard
    that can fit several TB in special RAM blocks, rivetet together
    by leftover watch-makers in Switzerland.

    On the other hand, if you plot your traffic in Gb/s, you probably
    need all the RAM you can afford/get.

... How can I limit Varnish to use less RAM ?

    You can not.  Varnish operates in Virtual Memory and it is up to the
    kernel to decide which process gets to use how much RAM to map the
    virtual address-space of the process.


Where...
========

... Can I find varnish for my operating system ?

    We know that Varnish has been packaged for Debian, Ubuntu, RHEL,
    Centos, (Open)SuSE, Gentoo and FreeBSD, possibly more.  Check whatever
    packagemanager you use.

Can I...
========

... Can I use Varnish as a client-side proxy ?

    No.  Varnish needs all backends configured in the VCL.  Look at squid
    instead.

... Can I run Varnish on a 32bit system ?

    Yes, recently somebody even claimed to run Varnish on his N900 mobile
    phone recently, but if you have the choice, go 64 bit from the start.

    Varnish is written to use Virtual Memory and on a 32bit system that
    really cramps your style, and you will have trouble configuring more
    than 2 GB of storage.

... Can I run Varnish on the same system as Apache ?

    Yes, and many people do that with good success.

    There will be competition for resources, but Apache is not particular
    good at using RAM effectively and Varnish is, so this synergy usually
    more than compensates for the competition.

... Can I run multiple Varnish on the same system ?

    Yes, as long as you give them different TCP ports and different ```-n```
    arguments, you will be fine.


... Can I cache multiple vhosts with one Varnish ?

    Yes, that works right out of the box.

... Can I see what is cached in Varnish ?

    That is not possible for several reasons.  A command to list
    all the contents of a Varnish cache with millions of objects would
    bring your Varnish to a standstill while it traverses the index.

    Besides, the output is a lot less useful than you might think.

... Can I use Varnish to do HTTPS ?

    Not at present, and while we keep an eye on this, there are no
    current plans to add HTTPS support, until we can find a way where
    it adds significant value, relative to running a stand-alone
    HTTPS proxy such as ngnix or pound.

... Can Varnish load balance between multiple backends ?

    Yes, you need VCL code like this::

	director foobar round-robin {
	    { .backend = { .host = "www1.example.com; .port = "http"; } }
	    { .backend = { .host = "www2.example.com; .port = "http"; } }
	}

	sub vcl_recv {
		set req.backend = foobar;
	}

    (XXX: reference to docs, once written)

Why ...
=======

... Varnish does not cache things, all requests hit the backend

    The number one cause is cookies, the ```default.vcl```  will
    not cache anything if the request has a ```Cookie:``` header
    or the if the backend sends a ```Set-Cookie:``` header.

    Number two cause is authentication, same thing.

... Why are regular expressions case-sensitive ?

    Some HTTP headers, such as ```Host:``` and ```Location:```
    contain FQDN's which by definition is not case-sensitive.  Other
    HTTP headers are case-sensitive, most notably the URLs.  Therefore
    a "one size fits all" solution is not possible.

    In previous releases, we used the POSIX regular expressions
    supplied with the operating system, and decided, because the
    most common use of regexps were on ```Host:``` headers, that
    they should not be case-sensitive.

    From version 2.1.0 and forward, we use PCRE regular expressions,
    where it *is* possible to control case-sensitivity in the
    individual regular expressions, so we decided that it would
    probably confuse people if we made the default case-insentive.
    (We promise not to change our minds about this again.)

    To make a PCRE regex case insensitive, put ```(?i)``` at the start::

	if (req.http.host ~ "?iexample.com$") {
		...
	}

    See the [http://www.pcre.org/pcre.txt PCRE man pages] for more information.


... Why does the ```Via:``` header say 1.1 in Varnish 2.1.x ?

    The number in the ```Via:``` header is the HTTP protocol version
    supported/applied, not the softwares version number.

... Why did you call it *Varnish* ?

    Long story, but basically the instigator of Varnish spent a long
    time staring at an art-poster with the word "Vernisage" and ended
    up checking it in a dictionary, which gives the following three
    meanings of the word:

	 r.v. var·nished, var·nish·ing, var·nish·es

	  1. To cover with varnish.
	  2. To give a smooth and glossy finish to.
	  3. To give a deceptively attractive appearance to; gloss over.

    The three point describes happens to your backend system when you
    put Varnish in front of it.
