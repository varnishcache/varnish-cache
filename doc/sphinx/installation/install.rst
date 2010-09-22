.. _install-doc:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Installing Varnish on your computer
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

With open source software, you can choose to install binary
packages or compile stuff from source-code. 

In general, from a point of principle, I would argue that
everybody should compile from source, but realistically
binary packages are *so much easier* so lets cover that first:


Installing Varnish from packages
================================

Installing Varnish on most relevant operating systems can usually 
be done with with the systems package manager, typical examples
being:

**FreeBSD**

	FreeBSD (from source)
		``cd /usr/ports/varnish && make install clean``
	FreeBSD (binary package)
		``pkg_add -r varnish``

**CentOS/RedHat 5.4 - RPM files**

	We try to keep the lastest version available as prebuildt RPMs (el4 & el5) on `SourceForge <http://sourceforge.net/projects/varnish/files/>`_.

	Varnish is included in the `EPEL <http://fedoraproject.org/wiki/EPEL>`_ repository. **BUT** unfortunatly we had a syntax change in Varnish 2.0.6->2.1.X. This means that we can not update Varnish in `EPEL <http://fedoraproject.org/wiki/EPEL>`_ so the latest version there is Varnish 2.0.6. In the future (EPEL6) we should be available with Varnish 2.1.X or higher.

**Debian/Ubuntu - DEB files**

	Varnish is distributed to the *unstable* repository of Debian. You should be able to get a hold of the lastest version there.
	
	Ubuntu syncronize the *unstable* Debian repository. See `Ubuntu Packages <http://packages.ubuntu.com/>`_.

**Other systems**

	You are probably best of compiling your own code. See `Compiling Varnish from source`_.

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

To get the development source code::

	svn co http://varnish-cache.org/svn/varnish/trunk

or if you want the production branch::

	svn co http://varnish-cache.org/svn/varnish/branches/2.1

Next, configuration:  For this you will need ``libtoolize``, ``aclocal``,
``autoheader``, ``automake`` and ``autoconf``, also known as *the
autocrap tools* installed on your system.

Once you have them::

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

And finally, the true test of a brave heart::

	make install

.. _SubVersion: http://subversion.tigris.org/
