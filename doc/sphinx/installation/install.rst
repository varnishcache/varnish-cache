.. _install-doc:

Installing Varnish
==================

With open source software, you can choose to install binary packages
or compile stuff from source-code. To install a package or compile
from source is a matter of personal taste. If you don't know which
method to choose, read the whole document and choose the method you
are most comfortable with.


Source or packages?
-------------------

Installing Varnish on most relevant operating systems can usually 
be done with with the systems package manager, typical examples
being:

FreeBSD
-------

Binary package:
		``pkg_add -r varnish``
From source:
		``cd /usr/ports/varnish && make install clean``

CentOS/RedHat
-------------

We try to keep the latest version available as prebuilt RPMs (el5 and el6)
on `repo.varnish-cache.org <http://repo.varnish-cache.org/>`_.  See the
`RedHat installation instructions
<http://www.varnish-cache.org/installation/redhat>`_ for more information.

Varnish is included in the `EPEL
<http://fedoraproject.org/wiki/EPEL>`_ repository, however due to
incompatible syntax changes in newer versions of Varnish, only older
versions are available. We recommend that you install the latest
version from our repository.

Debian/Ubuntu
-------------

Varnish is distributed with both Debian and Ubuntu. In order to get
Varnish up and running type `sudo apt-get install varnish`. Please
note that this might not be the latest version of Varnish.  If you
need a later version of Varnish, please follow the installation
instructions for `Debian
<http://www.varnish-cache.org/installation/debian>`_ or `Ubuntu
<http://www.varnish-cache.org/installation/ubuntu>`_.


Compiling Varnish from source
=============================

If there are no binary packages available for your system, or if you
want to compile Varnish from source for other reasons, follow these
steps:

We recommend downloading a release tarball, which you can find on
`repo.varnish-cache.org <http://repo.varnish-cache.org/source/>`_.

Alternatively, if you want to hack on Varnish, you should clone our
git repository by doing.

      git clone git://git.varnish-cache.org/varnish-cache

Please note that a git checkout will need some more build-dependencies
than listed below, in particular the Python Docutils and Sphinx.

Build dependencies on Debian / Ubuntu 
--------------------------------------

In order to build Varnish from source you need a number of packages
installed. On a Debian or Ubuntu system these are:

* autotools-dev
* automake1.11
* libtool 
* autoconf
* libncurses-dev
* groff-base
* libpcre3-dev
* pkg-config
* make
* libedit-dev

If you're building from git, you also need the following:

* python-docutils
* python-sphinx (optional, if you want the HTML docs built)

Build dependencies on Red Hat / CentOS
--------------------------------------

To build Varnish on a Red Hat or CentOS system you need the following
packages installed:

* automake 
* autoconf 
* libtool
* ncurses-devel
* groff
* pcre-devel
* pkgconfig
* libedit-devel

If you're building from git, you also need the following:

* docutils
* python-sphinx (optional, if you want the HTML docs built)

Configuring and compiling
-------------------------

Next, configuration: The configuration will need the dependencies
above satisfied. Once that is taken care of::

	cd varnish-cache
	sh autogen.sh
	sh configure
	make

The ``configure`` script takes some arguments, but more likely than
not, you can forget about that for now, almost everything in Varnish
are run time parameters.

Before you install, you may want to run the test suite, make a cup of
tea while it runs, it takes some minutes::

	make check

Don't worry if a single or two tests fail, some of the tests are a
bit too timing sensitive (Please tell us which so we can fix it) but
if a lot of them fails, and in particular if the ``b00000.vtc`` test 
fails, something is horribly wrong, and you will get nowhere without
figuring out what.

Installing
----------

And finally, the true test of a brave heart::

	make install

Varnish will now be installed in /usr/local. The varnishd binary is in
/usr/local/sbin/varnishd and its default configuration will be
/usr/local/etc/varnish/default.vcl. 

You can now proceed to the :ref:`tutorial-index`. 
