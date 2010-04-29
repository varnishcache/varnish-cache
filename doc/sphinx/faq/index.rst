
.. _faq:

Frequently Asked Varnish Questions
==================================

Varnish does...
---------------

... Varnish does not cache things, all requests hit the backend

    The number one cause is cookies, the ```default.vcl```  will
    not cache anything if the request has a ```Cookie:``` header
    or the if the backend sends a ```Set-Cookie:``` header.

    Number two cause is authentication, same thing.

How...
------

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
--------

... Can I find varnish for my operating system ?

    We know that Varnish has been packaged for Debian, Ubuntu, RHEL,
    Centos, (Open)SuSE, Gentoo and FreeBSD, possibly more.  Check whatever
    packagemanager you use.

Can I...
--------

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
-------

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

.. todo::


The old Trac FAQ, still to be reformattet:
------------------------------------------

::

	=== Does Varnish support compression? ===

	This is a simple question with a complicated answer; see [wiki:FAQ/Compression].

	=== Where can I find the log files? ===

	Varnish does not log to a file, but to shared memory log. Use the varnishlog utility to print the shared memory log or varnishncsa to present it in the Apache / NCSA "combined" log format.

	=== What is the purpose of the X-Varnish HTTP header? ===

	The X-Varnish HTTP header allows you to find the correct log-entries for the transaction. For a cache hit, X-Varnish will contain both the ID of the current request and the ID of the request that populated the cache. It makes debugging Varnish a lot easier.

	== Configuration ==

	== VCL ==

	=== How do I load VCL file while Varnish is running? ===

	 1. Place the VCL file on the server
	 1. Telnet into the managment port.
	 1. do a "vcl.load <configname> <filename>" in managment interface. <configname> is whatever you would like to call your new configuration.
	 1. do a "vcl.use <configname>" to start using your new config.


	=== Does Varnish require the system to have a C compiler? ===

	Yes.  The VCL compiler generates C source as output, and uses the systems C-compiler to compile that into a shared library.  If there is no C compiler, Varnish will not work.

	=== ... Isn't that security problem? ===

	The days when you could prevent people from running non-approved programs by removing the C compiler from your system ended roughly with the VAX 11/780 computer.

	=== What is a VCL file? ===

	VCL is an acronym for Varnish Configuration Language.  In a VCL file, you configure how Varnish should behave.  Sample VCL files will be included in this Wiki at a later stage.

	=== Where is the documentation on VCL? ===

	Please see "man 7 vcl".

	=== Should I use ''pipe'' or ''pass'' in my VCL code? What is the difference? ===

	When varnish does a ''pass'' it acts like a normal HTTP proxy. It
	reads the request and pushes it onto the backend. The next HTTP
	request can then be handled like any other.

	''pipe'' is only used when Varnish for some reason can't handle the
	''pass''. ''pipe'' reads the request, pushes in onty the backend
	_only_ pushes bytes back and forth, with no other actions taken.

	Since most HTTP clients do pipeline several requests into one
	connection this might give you an undesirable result - as every
	subsequent request will reuse the existing ''pipe''.

	Varnish versions prior to 2.0 does not support handling a request body
	with ''pass'' mode, so in those releases ''pipe'' is required for
	correct handling.

	In 2.0 and later, ''pass'' will handle the request body correctly.

	If you get 503 errors when making a request which is ''pass''ed, make sure
	that you're specifying the backend before returning from vcl_recv with ''pass''.


	=== Are regular expressions case sensitive or not? Can I change it? ===

	In 2.1 and newer, regular expressions are case sensitive by default.  In earlier versions, they were case insensitive.

	To change this for a single regex in 2.1, use "(?i)" at the start.  See the [http://www.pcre.org/pcre.txt PCRE man pages] for more information.

	== How do I... ==

	=== How can I force a refresh on a object cached by varnish? ===

	Refreshing is often called [http://dictionary.reference.com/browse/PURGE purging] a document.  You can purge at least 2 different ways in Varnish:

	1. From the command line you can write:

	{{{
	url.purge ^/$
	}}}

	to purge your '''/''' document.  As you might see url.purge takes an
	[http://en.wikipedia.org/wiki/Regular_expression regular expression]
	as its argument. Hence the !^ and $ at the front and end.  If the !^ is ommited, all the documents ending in a / in the cache would be deleted.

	So to delete all the documents in the cache, write:

	{{{
	url.purge .*
	}}}

	at the command line.

	2. HTTP PURGE

	VCL code to allow HTTP PURGE [wiki:VCLExamples is to be found here]. Note that this method does not support wildcard purging.

	=== How can I debug the requests of a single client? ===

	The "varnishlog" utility may produce a horrendous amount of output.  To be able debug our own traffic can be useful.

	The ReqStart token will include the client IP address.  To see log entries matching this, type:

	{{{
	$ varnishlog -c -o ReqStart 192.0.2.123
	}}}

	To see the backend requests generated by a client IP address, we can match on the TxHeader token, since the IP address of the client is included in the X-Forwarded-For header in the request sent to the backend.

	At the shell command line, type:
	{{{
	$ varnishlog -b -o TxHeader 192.0.2.123
	}}}

	=== How can I rewrite URLS before they are sent to the backend? ===

	You can use the "regsub()" function to do this.  Here's an example for zope, to rewrite URL's for the virtualhostmonster:

	{{{
	if (req.http.host ~ "^(www.)?example.com") {
	  set req.url = regsub(req.url, "^", "/VirtualHostBase/http/example.com:80/Sites/example.com/VirtualHostRoot");
	} 

	}}}

	=== I have a site with many hostnames, how do I keep them from multiplying the cache? ===

	You can do this by normalizing the "Host" header for all your hostnames.  Here's a VCL example:

	{{{
	if (req.http.host ~ "^(www.)?example.com") {
	  set req.http.host = "example.com";
	} 
	}}}

	=== How can I log the client IP address on the backend? ===

	All I see is the IP address of the varnish server.  How can I log the client IP address?

	We will need to add the IP address to a header used for the backend request, and configure the backend to log the content of this header instead of the address of the connecting client (which is the varnish server).

	Varnish configuration:
	{{{
	sub vcl_recv {
	  # Add a unique header containing the client address
	  remove req.http.X-Forwarded-For;
	  set    req.http.X-Forwarded-For = client.ip;
	  # [...]
	}
	}}}

	For the apache configuration, we copy the "combined" log format to a new one we call "varnishcombined", for instance, and change the client IP field to use the content of the variable we set in the varnish configuration:
	{{{
	LogFormat "%{X-Forwarded-For}i %l %u %t \"%r\" %>s %b \"%{Referer}i\" \"%{User-Agent}i\"" varnishcombined
	}}}

	And so, in our virtualhost, you need to specify this format instead of "combined" (or "common", or whatever else you use)
	{{{
	<VirtualHost *:80>
	  ServerName www.example.com
	  # [...]
	  CustomLog /var/log/apache2/www.example.com/access.log varnishcombined
	  # [...]
	</VirtualHost>
	}}}

	The [http://www.openinfo.co.uk/apache/index.html mod_extract_forwarded Apache module] might also be useful.

	=== How do I add a HTTP header? ===

	To add a HTTP header, unless you want to add something about the client/request, it is best done in vcl_fetch as this means it will only be processed every time the object is fetched:

	{{{
	sub vcl_fetch {
	  # Add a unique header containing the cache servers IP address:
	  remove obj.http.X-Varnish-IP;
	  set    obj.http.X-Varnish-IP = server.ip;
	  # Another header:
	  set    obj.http.Foo = "bar";
	}
	}}}

	=== How do I do to alter the request going to the backend? ===
	You can use the ''bereq'' object for altering requests going to the backend but from my experience you can only 'set' values to it.
	So, if you need to change the requested URL, '''this doesn't work''':

	{{{
	sub vcl_miss {
		set bereq.url = regsub(bereq.url,"stream/","/");
		fetch;
	}
	}}}

	Because you cannot read from bereq.url (in the value part of the assignment). You will get:
	{{{
	mgt_run_cc(): failed to load compiled VCL program:
	  ./vcl.1P9zoqAU.o: undefined symbol: VRT_r_bereq_url
	VCL compilation failed
	}}}

	Instead, you have to use '''req.url''':

	{{{
	sub vcl_miss {
		set bereq.url = regsub(req.url,"stream/","/");
		fetch;
	}
	}}}

	=== How do I force the backend to send Vary headers? ===

	We have anectdotal evidence of non-RFC2616 compliant backends, which support content negotiation, but which do not emit a Vary header, unless the request contains Accept headers.

	It may be appropriate to send no-op Accept headers to trick the backend into sending us the Vary header.

	The following should be sufficient for most cases:

	{{{
	Accept: */*
	Accept-Language: *
	Accept-Charset: *
	Accept-Encoding: identity
	}}}

	Note that Accept-Encoding can not be set to *, as the backend might then send back a compressed response which the client would be unable to process.

	This can of course be implemented in VCL.

	=== How can I customize the error messages that Varnish returns? ===

	A custom error page can be generated by adding a vcl_error to your configuration file. The default error page looks like this: 

	{{{
	sub vcl_error {
	    set obj.http.Content-Type = "text/html; charset=utf-8";

	    synthetic {"
	    <?xml version="1.0" encoding="utf-8"?>
	    <!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN"
	     "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
	    <html>
	      <head>
		<title>"} obj.status " " obj.response {"</title>
	      </head>
	      <body>
	      <h1>Error "} obj.status " " obj.response {"</h1>
	      <p>"} obj.response {"</p>
		<h3>Guru Meditation:</h3>
		<p>XID: "} req.xid {"</p>
		<address><a href="http://www.varnish-cache.org/">Varnish</a></address>
	      </body>
	     </html>
	     "};
	    deliver;
	}
	}}}

	=== How do I instruct varnish to ignore the query parameters and only cache one instance of an object? ===

	This can be achieved by removing the query parameters using a regexp:

	{{{
	sub vcl_recv {
	    set req.url = regsub(req.url, "\?.*", "");
	}
	}}}

	=== Do you have any examples? ===

	Many varnish users have contributed [wiki:VCLExamples examples] from their VCLs to solve common problems. A ready made recipe to address your question may be included.

	== Troubleshooting ==

	=== Why does it look like Varnish sends all requests to the backend? I thought it was a cache? ===

	There are 2 common reasons for this:
	 1. The object's '''ttl expired'''. A common situation is that the backend does not set an expiry time on the requested image/file/webpage, so Varnish uses the default TTL (normally 120s). 
	 2. Your site uses '''cookies''':
	    * By default, varnish will not cache ''responses'' from the backend that come with a '''Set-Cookie''': header.
	    * By default, varnish will not serve ''requests'' with a '''Cookie:''' header, but pass them to the backend instead. Check out [wiki:VCLExamples these VCL examples] on how to make varnish cache cookied/logged in users sanely.

	=== Why am I getting a cache hit, but a request is still going to my backend? ===

	Varnish has a feature called ''hit for pass'', which is used when Varnish gets a response from the backend and finds out it cannot be cached. In such cases, Varnish will create a cache object that records that fact, so that the next request goes directly to "pass".
	See the entry above for common cases where a backend returns a non-cacheable object. See this [wiki:VCLExampleDefault graphical overview] of how the Varnish request cycle works.

	Since Varnish bundles multiple requests for the same URL to the backend, a common case where a client will get a ''hit for pass'' is:
	  * Client 1 requests url /foo
	    * Client 2..N request url /foo
	  * Varnish tasks a worker to fetch /foo for Client 1
	    * Client 2..N are now queued pending response from the worker
	  * Worker returns object to varnish which turns out to be non-cacheable.
	    * Client 2..N are now given the ''hit for pass'' object instructing them to go to the backend

	The ''hit for pass'' object will stay cached for the duration of it's ttl. This means that subsequent clients requesting /foo will be sent straight to the backend as long as the ''hit for pass'' object exists.
	The [wiki:StatsExplained varnishstat program] can tell you how many ''hit for pass'' objects varnish has served. You can lower the ttl for such an object if '''you are sure this is needed''', using the following logic:

	{{{
	sub vcl_fetch {
	  if (!obj.cacheable) {
	    # Limit the lifetime of all 'hit for pass' objects to 10 seconds
	    obj.ttl = 10s;
	    pass;
	  }
	}

	}}}

