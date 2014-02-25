=============
varnishreplay
=============

------------------------
HTTP traffic replay tool
------------------------

SYNOPSIS
========
varnishreplay [-D] -a address:port -r file

DESCRIPTION
===========

The varnishreplay utility parses Varnish logs and attempts to
reproduce the traffic. It is typically used to *warm* up caches or
various forms of testing.

The following options are available:

-a backend           Send the traffic over tcp to this server, specified by an
   		     address and a port.  This option is
   		     mandatory. Only IPV4 is supported at this time.

-D                   Turn on debugging mode.

-r file              Parse logs from this file.  The input file has to be from
		     a varnishlog of the same version as the varnishreplay
		     binary.  This option is mandatory.

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

* Copyright (c) 2007-2014 Varnish Software AS
