..
	Copyright (c) 2010-2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _install-doc:

Installing Varnish
==================

.. no section heading here.

With open source software, you can choose to install binary packages or compile
it yourself from source code. To install a package or compile from source is a
matter of personal taste. If you don't know which method to choose, we
recommend that you read this whole section and then choose the method you feel
most comfortable with.

Unfortunately, something as basic as installing a piece of software
is highly operating system specific:

.. toctree::
	:maxdepth: 2

	install_debian
	install_freebsd
	install_openbsd
	install_redhat

Compiling Varnish from source
=============================

If there are no binary packages available for your system, or if you
want to compile Varnish from source for other reasons:

.. toctree::
	:maxdepth: 2

	install_source

Other pre-built Varnish packages
================================

Here is a list of the ones we know about:

* `ArchLinux package`_ and `ArchLinux wiki`_
* `Alpine Linux`_
* `UPLEX Packages`_ with various vmods for Debian, Ubuntu and RHEL/CentOS

.. _`ArchLinux package`: https://www.archlinux.org/packages/extra/x86_64/varnish/
.. _`ArchLinux wiki`: https://wiki.archlinux.org/index.php/Varnish
.. _`Alpine Linux`: https://pkgs.alpinelinux.org/package/edge/main/x86_64/varnish
.. _`UPLEX Packages`: https://pkg.uplex.de/

