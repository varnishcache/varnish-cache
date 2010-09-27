===========
varnishhist
===========

:Author: Dag-Erling Smørgrav
:Date:   2010-05-31
:Version: 1.0
:Manual section: 1


-------------------------
Varnish request histogram
-------------------------

SYNOPSIS
========

varnishhist [-b] [-C] [-c] [-d] [-I regex] [-i tag] [-n varnish_name] 
[-r file] [-V] [-w delay] [-X regex] [-x tag]

DESCRIPTION
===========

The varnishhist utility reads varnishd(1) shared memory logs and
presents a continuously updated histogram show‐ ing the distribution
of the last N requests by their processing.  The value of N and the
vertical scale are dis‐ played in the top left corner.  The horizontal
scale is logarithmic.  Hits are marked with a pipe character ("|"),
and misses are marked with a hash character ("#").

The following options are available:

-b          Include log entries which result from communication with 
	    a backend server.  If neither -b nor -c is
	    specified, varnishhist acts as if they both were.

-C          Ignore case when matching regular expressions.

-c          Include log entries which result from communication with 
	    a client.  If neither -b nor -c is specified, 
	    varnishhist acts as if they both were.

-d          Process old log entries on startup.  Normally, varnishhist 
	    will only process entries which are written to the 
	    log after it starts.

-I regex    Include log entries which match the specified 
   	    regular expression.  If neither -I nor -i is specified, 
	    all log entries are included.

-i tag      Include log entries with the specified tag.  If neither 
   	    -I nor -i is specified, all log entries are included.

-n          Specifies the name of the varnishd instance to get logs 
	    from.  If -n is not specified, the host name is used.

-r file     Read log entries from file instead of shared memory.

-V          Display the version number and exit.

-w delay    Wait at least delay seconds between each update.  The 
   	    default is 1.  file instead of displaying them.  The file 
	    will be overwritten unless the -a option was specified.

-X regex    Exclude log entries which match the specified regular expression.

-x tag      Exclude log entries with the specified tag.

SEE ALSO
========

* varnishd(1)
* varnishlog(1)
* varnishncsa(1)
* varnishstat(1) 
* varnishtop(1)

HISTORY
=======
The varnishhist utility was developed by Poul-Henning Kamp in cooperation with Verdens Gang
AS and Linpro AS.  This manual page was written by Dag-Erling Smørgrav.

COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2006 Verdens Gang AS
* Copyright (c) 2006-2008 Linpro AS
* Copyright (c) 2008-2010 Redpill Linpro AS
* Copyright (c) 2010 Varnish Software AS
