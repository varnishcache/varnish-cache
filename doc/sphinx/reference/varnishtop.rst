============
varnishtop
============

-------------------------
Varnish log entry ranking
-------------------------

SYNOPSIS
========

varnishtop [-1] [-b] [-C] [-c] [-d] [-f] [-I regex] 
[-i tag] [-n varnish_name] [-r file] [-V] [-X regex]
[-x tag]

DESCRIPTION
===========

The varnishtop utility reads varnishd(1) shared memory logs and
presents a continuously updated list of the most commonly occurring
log entries.  With suitable filtering using the -I, -i, -X and -x
options, it can be used to display a ranking of requested documents,
clients, user agents, or any other information which is recorded in
the log.

The following options are available:

-1          Instead of presenting of a continuously updated display, 
	    print the statistics once and exit. Implies -d.

-b          Include log entries which result from communication 
	    with a backend server.  If neither -b nor -c is
	    specified, varnishtop acts as if they both were.

-C          Ignore case when matching regular expressions.

-c          Include log entries which result from communication 
	    with a client.  If neither -b nor -c is specified, 
	    varnishtop acts as if they both were.

-d          Process old log entries on startup.  Normally, varnishtop 
	    will only process entries which are written to the log 
	    after it starts.

-f          Sort and group only on the first field of each log entry.  
	    This is useful when displaying e.g. stataddr entries, 
	    where the first field is the client IP address.

-I regex    Include log entries which match the specified regular 
   	    expression.  If neither -I nor -i is specified, all log 
	    entries are included.

-i tag      Include log entries with the specified tag.  If neither -I 
   	    nor -i is specified, all log entries are included.

-n          Specifies the name of the varnishd instance to get logs from.  
	    If -n is not specified, the host name is used.

-r file     Read log entries from file instead of shared memory.

-V          Display the version number and exit.

-X regex    Exclude log entries which match the specified regular expression.

-x tag      Exclude log entries with the specified tag.

EXAMPLES
========

The following example displays a continuously updated list of the most
frequently requested URLs:::

  varnishtop -i RxURL

The following example displays a continuously updated list of the most
commonly used user agents:::

  varnishtop -i RxHeader -C -I ^User-Agent

SEE ALSO
========

* varnishd(1)
* varnishhist(1)
* varnishlog(1)
* varnishncsa(1)
* varnishstat(1)

HISTORY
=======

The varnishtop utility was originally developed by Poul-Henning Kamp
in cooperation with Verdens Gang AS and Linpro AS, and later
substantially rewritten by Dag-Erling Smørgrav.  This manual page was
written by Dag-Erling Smørgrav.
