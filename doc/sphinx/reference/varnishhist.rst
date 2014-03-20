.. _ref-varnishhist:

===========
varnishhist
===========

-------------------------
Varnish request histogram
-------------------------

SYNOPSIS
========

.. include:: ../include/varnishhist_synopsis.rst
varnishhist |synopsis|

DESCRIPTION
===========

The varnishhist utility reads varnishd(1) shared memory logs and
presents a continuously updated histogram showing the distribution
of the last N requests by their processing.  The value of N and the
vertical scale are displayed in the top left corner.  The horizontal
scale is logarithmic.  Hits are marked with a pipe character ("|"),
and misses are marked with a hash character ("#").

The following options are available:

.. include:: ../include/varnishhist_options.rst

SEE ALSO
========

* varnishd(1)
* varnishlog(1)
* varnishncsa(1)
* varnishstat(1)
* varnishtop(1)

HISTORY
=======
The varnishhist utility was developed by Poul-Henning Kamp in cooperation with
Verdens Gang AS and Varnish Software AS. This manual page was written by
Dag-Erling Smørgrav.

COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2006 Verdens Gang AS
* Copyright (c) 2006-2014 Varnish Software AS
