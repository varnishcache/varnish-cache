..
	Copyright (c) 2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of licens

.. _install-freebsd:

Installing on FreeBSD
=====================

From package
------------

FreeBSD offers two versions of Varnish pre-packaged::

	pkg install varnish6

or, if for some reason you want the older version::

	pkg install varnish4

From ports
----------

The FreeBSD packages are built out of the "ports" tree, and you can
install varnish directly from ports if you prefer, for instance to
get a newer version of Varnish than the current set of prebuilt
packages provide::

	cd /usr/ports/www/varnish6
	make all install clean

