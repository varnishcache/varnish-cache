..
	Copyright (c) 2018 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _whatsnew_changes_6.1:

Changes in Varnish 6.1
======================

This release is a maintenance release, so while there are many actual
changes, and of course many bugfixes, they should not have little to no
impact on running Varnish installations.

Nothing to see here, really
---------------------------

Since new users often forget to `vcl.discard` their old VCLs, we have
added a warning when you have more than 100 VCLs loaded.  There are
parameters to set the threshold and decide what happens when it is
exceeded (ignore/warn/error).

We have made `req.http.Host` mandatory and handle requests without it
on the fast DoS avoidance path.

For all the details and new stuff, see :ref:`whatsnew_upgrading_6.1`

*eof*
