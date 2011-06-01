.. _reference-varnishstat:

===========
varnishstat
===========

---------------------------
Varnish Cache statistics
---------------------------

:Author: Dag-Erling Smørgrav
:Author: Per Buer
:Date:   2010-06-1
:Version: 1.0
:Manual section: 1


SYNOPSIS
========

varnishstat [-1] [-x] [-f field_list] [-l] [-n varnish_name] [-V] [-w delay]

DESCRIPTION
===========

The varnishstat utility displays statistics from a running varnishd(1) instance.

The following options are available:

-1          Instead of presenting of a continuously updated display, print the statistics once and exit.

-f          A comma separated list of the fields to display.  If it starts with '^' it is used as an exclusion
	    list.

-l          Lists the available fields to use with the -f option.

-n          Specifies the name of the varnishd instance to get logs from.  If -n is not specified, the host name
	    is used.

-V          Display the version number and exit.

-w delay    Wait delay seconds between updates.  The default is 1.

-x          Displays the result as XML once.

The columns in the main display are, from left to right:

1.   Value
2.   Per-second average in the period since last update, or a period if the value can not be averaged
3.   Per-second average over process lifetime, or a period if the value can not be averaged
4.   Descriptive text

When using the -1 option, the columns in the output are, from left to right:

1.   Symbolic entry name
2.   Value
3.   Per-second average over process lifetime, or a period if the value can not be averaged
4.   Descriptive text

When using the -x option, the output is::

  <stat> 
    <name>FIELD NAME</name> 
    <value>FIELD VALUE</value> 
    <description>FIELD DESCRIPTION</description> 
  </stat> 


SEE ALSO
========

* varnishd(1)
* varnishhist(1)
* varnishlog(1)
* varnishncsa(1)
* varnishtop(1)
* curses(3)

HISTORY
=======

The varnishstat utility was originally developed by Poul-Henning Kamp
⟨phk@phk.freebsd.dk⟩ in cooperation with Verdens Gang AS, Varnish Software AS
and Varnish Software. Manual page written by Dag-Erling Smørgrav,
and Per Buer. 

COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2006 Verdens Gang AS
* Copyright (c) 2006-2008 Varnish Software AS
