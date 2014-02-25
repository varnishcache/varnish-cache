========
vmod_std
========

-----------------------
Varnish Standard Module
-----------------------

SYNOPSIS
========

import std [from "path"] ;

DESCRIPTION
===========

Vmod_std contains basic functions which are part and parcel of Varnish,
but which for reasons of architecture fit better in a VMOD.

One particular class of functions in vmod_std is the conversions functions
which all have the form::

	TYPE type(STRING, TYPE)

These functions attempt to convert STRING to the TYPE, and if that fails,
they return the second argument, which must have the given TYPE.

FUNCTIONS
=========

toupper
-------

Prototype
	STRING toupper(STRING s)
Description
	Converts the string *s* to upper case.
Example
	set beresp.http.x-scream = std.toupper("yes!");

tolower
-------
Prototype
	STRING tolower(STRING s)
Description
	Converts the string *s* to lower case.
Example
	set beresp.http.x-nice = std.tolower("VerY");

set_ip_tos
----------
Prototype
	VOID set_ip_tos(INT i)
Description
	Sets the Type-of-Service flag for the current session. Please
	note that the TOS flag is not removed by the end of the
	request so probably want to set it on every request should you
	utilize it.
Example
	| if (req.url ~ ^/slow/) {
	|    std.set_ip_tos(0x0);
	| }

random
------
Prototype
	REAL random(REAL a, REAL b)
Description
	Returns a random REAL number between *a* and *b*.
Example
	set beresp.http.x-random-number = std.random(1, 100);

log
---
Prototype
	VOID log(STRING string)
Description
	Logs *string* to the shared memory log, using VSL tag *SLT_VCL_Log*.
Example
	std.log("Something fishy is going on with the vhost " + req.host);

syslog
------
Prototype
	VOID syslog(INT priority, STRING string)
Description
	Logs *string* to syslog marked with *priority*.  See your
	system's syslog.h file for the legal values of *priority*.
Example
	std.syslog(8 + 1, "Something is wrong");

fileread
--------
Prototype
	STRING fileread(STRING filename)
Description
	Reads a file and returns a string with the content. Please
	note that it is not recommended to send variables to this
	function the caching in the function doesn't take this into
	account. Also, files are not re-read.
Example
	set beresp.http.x-served-by = std.fileread("/etc/hostname");

collect
-------
Prototype
	VOID collect(HEADER header)
Description
	Collapses the header, joining the headers into one.
Example
	std.collect(req.http.cookie);
	This will collapse several Cookie: headers into one, long
	cookie header.


CONVERSION FUNCTIONS
====================

duration
--------
Prototype
	DURATION duration(STRING s, DURATION fallback)
Description
	Converts the string *s* to seconds. *s* must be quantified
	with ms (milliseconds), s (seconds), m (minutes), h (hours),
	d (days) or w (weeks) units. If *s* fails to parse,
	*fallback* will be returned.
Example
	set beresp.ttl = std.duration("1w", 3600s);

integer
--------
Prototype
	INT integer(STRING s, INT fallback)
Description
	Converts the string *s* to an integer.  If *s* fails to parse,
	*fallback* will be returned.
Example
	if (std.integer(beresp.http.x-foo, 0) > 5) { ... }

ip
--
Prototype
	IP ip(STRING s, IP fallback)
Description
	Converts string *s* to the first IP number returned by
	the system library function getaddrinfo(3).  If conversion
	fails, *fallback* will be returned.
Example
	if (std.ip(req.http.X-forwarded-for, "0.0.0.0") ~ my_acl) { ... }

healthy
-------
Prototype
	BOOL healthy(BACKEND backend)
 
Description
	Returns true if the backend is healthy.

port
----
Prototype
	INT port(IP ip)

Description
	Returns the port number of an IP address.


SEE ALSO
========

* vcl(7)
* varnishd(1)

HISTORY
=======

The Varnish standard module was released along with Varnish Cache 3.0.
This manual page was written by Per Buer with help from Martin Blix
Grydeland.

COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2011-2014 Varnish Software
