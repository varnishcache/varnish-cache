========
vmod_std
========

-----------------------
Varnish Standard Module
-----------------------

:Author: Per Buer
:Date:   2011-05-19
:Version: 1.0
:Manual section: 3


SYNOPSIS
========

import std

DESCRIPTION
===========

The Varnish standard module contains useful, generic function that
don't quite fit in the VCL core, but are still considered very useful
to a broad audience.

FUNCTIONS
=========

toupper
-------

Prototype
	toupper(STRING S)
Return value
       String
Description
	Converts the STRING S to upper case.
Example
	set beresp.http.x-scream = std.toupper("yes!");

tolower
-------
Prototype
	tolower(STRING S)
Return value
       String
Description
	Converts the STRING to lower case.
Example
        set beresp.http.x-nice = std.tolower("VerY");

set_up_tos
----------
Prototype
	set_ip_tos(INT I)
Return value
       Void
Description
	Sets the Type-of-Service flag for the current session. Please
	note that the TOS flag is not removed by the end of the
	request so probably want to set it on every request should you
	utilize it.
Example
	| if (req.url ~ ^/slow/) {
	|    std.set_up_tos(0x0);
	| }

random
------
Prototype
	random(REAL a, REAL b)
Return value
       Real
Description
	Returns a random REAL number between *a* and *b*.
Example
	set beresp.http.x-random-number = std.random(1, 100);

log
---
Prototype
	log(STRING string)
Return value
       Void
Description
	Logs string to the shared memory log.
Example
	std.log("Something fishy is going on with the vhost " + req.host);

syslog
------
Prototype
	syslog(INT priority, STRING string)
Return value
        Void
Description
	Logs *string* to syslog marked with *priority*.
Example
	std.syslog( LOG_USER|LOG_ALERT, "There is serious troble");

fileread
--------
Prototype
	fileread(STRING filename)
Return value
        String
Description
	Reads a file and returns a string with the content. Please
	note that it is not recommended to send variables to this
	function the caching in the function doesn't take this into
	account. Also, files are not re-read.
Example
	set beresp.http.x-served-by = std.fileread("/etc/hostname");

duration
--------
Prototype
	duration(STRING s, DURATION fallback)
Return value
       Duration
Description
	Converts the string s to seconds. s can be quantified with the
	usual s (seconds), m (minutes), h (hours), d (days) and w
	(weeks) units. If it fails to parse the string *fallback* 
	will be used
Example
	set beresp.ttl = std.duration("1w", 3600);

integer
--------
Prototype
	integer(STRING s, INT fallback)
Return value
       Int
Description
	Converts the string s to an integer.  If it fails to parse the
	string *fallback* will be used
Example
	if (std.integer(beresp.http.x-foo, 0) > 5) { … }

collect
-------
Prototype
	collect(HEADER header)
Return value
       Void
Description
	Collapses the header, joining the headers into one.
Example
	std.collect(req.http.cookie);
	This will collapse several Cookie: headers into one, long
	cookie header.

	
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

* Copyright (c) 2011 Varnish Software
