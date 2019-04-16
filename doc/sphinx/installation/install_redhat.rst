.. _install-redhat:

Installing on RedHat or CentOS
==============================

Varnish is included in the `EPEL
<https://fedoraproject.org/wiki/EPEL>`_ repository, however due to
incompatible syntax changes in newer versions of Varnish, only older
versions are available.

We therefore recommend that you install the latest version directly from our repository, as described above.

Varnish Cache is packaged in RPMs for easy installation and upgrade on Red Hat
systems. The Varnish Cache project maintains official packages for the current
Enterprise Linux versions. Varnish Cache 4.1 and 5.x are supported on EL6 and EL7.

We try to keep the latest version available as prebuilt RPMs (el5 and el6)
on `packagecloud.io/varnishcache <https://packagecloud.io/varnishcache/>`_.

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


Official packages of 4.1
------------------------

To use Varnish Cache 4.1 packages from the official varnish-cache.org repos,
follow the instructions available at:

* https://packagecloud.io/varnishcache/varnish41/install#manual-rpm

External packaging
------------------
Varnish Cache is also distributed in third party package repositories.

.. _`Fedora EPEL`: https://fedoraproject.org/wiki/EPEL

* `Fedora EPEL`_ does community packaging of Varnish Cache.

* RedHat has packaged versions of Varnish Cache available since Software Collections 2.1. Announcement on <http://developers.redhat.com/blog/2015/11/17/software-collections-2-1-generally-available/>.
