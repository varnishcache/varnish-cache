..
	Copyright (c) 2019-2020 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _install-redhat:

Installing on RedHat or CentOS
==============================

Varnish is included in the `EPEL
<https://fedoraproject.org/wiki/EPEL>`_ repository, however due to
incompatible syntax changes in newer versions of Varnish, only older
versions are available.

We therefore recommend that you install the latest version directly from our
repository, as described above.

Varnish Cache is packaged in RPMs for easy installation and upgrade on Red Hat
systems. The Varnish Cache project maintains official packages for the current
Enterprise Linux versions. Varnish Cache 6.x series are supported on el7.

We try to keep the latest version available as prebuilt RPMs on
`packagecloud.io/varnishcache <https://packagecloud.io/varnishcache/>`_.

We no longer provide RPM packages for el8 or later.

Official packages of 6
----------------------

Starting from Varnish Cache 5.0, we've simplified our packaging down to two:
the main package and a development package.

The official Varnish Cache repository is now hosted at Packagecloud.io.
Note that while Packagecloud.io provides Bash Script installs, we recommend
using the manual installation procedures.

Instructions for installing the official repository which contains the newest
Varnish Cache 6 release are available at:

* https://packagecloud.io/varnishcache/varnish60lts/install#manual-rpm

With the release of 6.0.2, users have to switch to switch repositories to get
the latest version.
Read more about this on `Release 6.0.2 </releases/rel6.0.2>`_.

We still provide el8 packages for Varnish Cache 6.0 LTS, but so does Red Hat.
Their el8 package is provided as a DNF module that inhibits our packages, and
the solution is to disable the module before installing::

    dnf module disable varnish

External packaging
------------------

Varnish Cache is also distributed in third party package repositories.

.. _`Fedora EPEL`: https://fedoraproject.org/wiki/EPEL

.. _`Software Collections 2.1`: http://developers.redhat.com/blog/2015/11/17/software-collections-2-1-generally-available/

.. _`provides`: https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/8/html/8.0_release_notes/rhel-8_0_0_release#BZ-1633338


* `Fedora EPEL`_ does community packaging of Varnish Cache.

* Red Hat has packaged versions of Varnish Cache available since
  `Software Collections 2.1`_.

* Red Hat provides_ Varnish Cache 6.0 LTS on el8.
