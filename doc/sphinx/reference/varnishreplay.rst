=============
varnishreplay
=============

------------------------
HTTP traffic replay tool
------------------------

:Author: Cecilie Fritzvold
:Author: Per Buer
:Date:   2010-05-31
:Version: 1.0
:Manual section: 1


SYNOPSIS
========
varnishreplay [-D] -a address:port -r file

DESCRIPTION
===========

The varnishreplay utility parses varnish logs and attempts to
reproduce the traffic. It is typcally used to *warm* up caches or
various forms of testing.

The following options are available:

-a backend           Send the traffic over tcp to this server, specified by an 
   		     address and a port.  This option is 
   		     mandatory. Only IPV4 is supported at this time.

-D                   Turn on debugging mode.

-r file              Parse logs from this file.  This option is mandatory.


SEE ALSO
========

* varnishd(1)
* varnishlog(1)

HISTORY
=======

The varnishreplay utility and this manual page were written by Cecilie
Fritzvold and later updated by Per Buer.

COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2007 Linpro AS
* Copyright (c) 2010 Varnish Software AS
