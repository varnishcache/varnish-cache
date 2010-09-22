.. _install-doc:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Installing Varnish on your computer
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

With open source software, you can choose to install binary
packages or compile stuff from source-code. 

Installing Varnish from packages
================================

Installing Varnish on most relevant operating systems can usually 
be done with with the systems package manager, typical examples
being:

FreeBSD
~~~~~~~

From source:
		``cd /usr/ports/varnish && make install clean``
Binary package:
		``pkg_add -r varnish``

CentOS/RedHat 5.4
~~~~~~~~~~~~~~~~~

We try to keep the lastest version available as prebuildt RPMs (el4 &
el5) on `SourceForge <http://sourceforge.net/projects/varnish/files/>`_.

Varnish is included in the `EPEL
<http://fedoraproject.org/wiki/EPEL>`_ repository.  Unfortunatly we
had a syntax change in Varnish 2.0.6->2.1.X. This means that we can
not update Varnish in `EPEL <http://fedoraproject.org/wiki/EPEL>`_ so
the latest version there is Varnish 2.0.6.

EPEL6 should have Varnish 2.1 available once it releases. 

Debian/Ubuntu - DEB files
~~~~~~~~~~~~~~~~~~~~~~~~~

Varnish is distributed with both Debian and Ubuntu. In order to get
Varnish up and running type `sudo apt-get install varnish`. Please
note that this might not be the latest version of Varnish.

Other systems
~~~~~~~~~~~~~

You are probably best of compiling your own code. See `Compiling
Varnish from source`_.

If that worked for you, you can skip the rest of this document
for now, and and start reading the much more interesting :ref:`tutorial-index`
instead.


Compiling Varnish from source
=============================

If there are no binary packages available for your system, or if you
want to compile Varnish from source for other reasons, follow these
steps:

First get a copy of the sourcecode using the ``svn`` command.  If
you do not have this command, you need to install SubVersion_ on
your system.  There is usually a binary package, try substituting
"subversion" for "varnish" in the examples above, it might just work.

To fetch the current (2.1) production branch:::

	svn co http://varnish-cache.org/svn/varnish/branches/2.1

To get the development source code:::

	svn co http://varnish-cache.org/svn/varnish/trunk

Build dependencies on Debian / Ubuntu 
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In order to build Varnish from source you need a number of packages
installed. On a Debian or Ubuntu system these are:

* autotools-dev
* automake1.9
* libtool 
* autoconf
* libncurses-dev
* xsltproc
* groff-base
* libpcre3-dev

To install all these just type ``sudo apt-get install autotools-dev automake1.9 libtool autoconf libncurses-dev xsltproc groff-base libpcre3-dev``. 

Build dependencies on Red Hat / Centos
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To build Varnish on a Red Hat or Centos system you need the following
packages installed:

* automake 
* autoconf 
* libtool
* ncurses-devel
* libxslt
* groff
* pcre-devel
* pkgconfig

Configuring and compiling
~~~~~~~~~~~~~~~~~~~~~~~~~

Next, configuration: The configuration will need the dependencies
above satisfied. Once that is take care of:::

	cd varnish-cache
	sh autogen.sh
	sh configure
	make

The ``configure`` script takes some arguments, but more likely than
not, you can forget about that for now, almost everything in Varnish
are runtime parameters.

Before you install, you may want to run the regression tests, make
a cup of tea while it runs, it takes some minutes::

	(cd bin/varnishtest && ./varnishtest tests/*.vtc)

Don't worry of a single or two tests fail, some of the tests are a
bit too timing sensitive (Please tell us which so we can fix it) but
if a lot of them fails, and in particular if the ``b00000.vtc`` test 
fails, something is horribly wrong, and you will get nowhere without
figuring out what.

Installing
~~~~~~~~~~

And finally, the true test of a brave heart::

	make install

Varnish will now be installed in /usr/local. The varnishd binary is in
/usr/local/sbin/varnishd and its 

.. _SubVersion: http://subversion.tigris.org/
