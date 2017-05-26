.. _install-doc:

Installing Varnish
==================

.. no section heading here.

With open source software, you can choose to install binary packages or compile
it yourself from source code. To install a package or compile from source is a
matter of personal taste. If you don't know which method to choose, we
recommend that you read this whole section and then choose the method you feel
most comfortable with.


Source or packages?
-------------------

Installing Varnish on most relevant operating systems can usually
be done with with the specific systems package manager, typical examples
being:

FreeBSD
-------

Binary package:
		``pkg_add -r varnish``
From source:
		``cd /usr/ports/varnish && make install clean``

Red Hat / CentOS
----------------

We try to keep the latest version available as prebuilt RPMs (el5 and el6)
on `repo.varnish-cache.org <http://repo.varnish-cache.org/>`_.  See the online
`Red Hat installation instructions
<https://www.varnish-cache.org/installation/redhat>`_ for more information.

Varnish is included in the `EPEL
<https://fedoraproject.org/wiki/EPEL>`_ repository, however due to
incompatible syntax changes in newer versions of Varnish, only older
versions are available.

We therefore recommend that you install the latest version directly from our repository, as described above.

Debian/Ubuntu
-------------

Varnish is distributed with both Debian and Ubuntu. In order to get
Varnish up and running type ``sudo apt-get install varnish``. Please
note that this might not be the latest version of Varnish.  If you
need a later version of Varnish, please follow the online installation
instructions for `Debian
<https://www.varnish-cache.org/installation/debian>`_ or `Ubuntu
<https://www.varnish-cache.org/installation/ubuntu>`_.


Compiling Varnish from source
=============================

If there are no binary packages available for your system, or if you
want to compile Varnish from source for other reasons, follow these
steps:

Download the appropriate release tarball, which you can find on
http://repo.varnish-cache.org/source/ .

Alternatively, if you want to hack on Varnish, you should clone our
git repository by doing.

      ``git clone git://git.varnish-cache.org/varnish-cache``


Build dependencies on Debian / Ubuntu
--------------------------------------

In order to build Varnish from source you need a number of packages
installed. On a Debian or Ubuntu system these are:

..  grep-dctrl -n -sBuild-Depends -r ^ ../../../../varnish-cache-debian/control | tr -d '\n' | awk -F,\  '{ for (i = 0; ++i <= NF;) { sub (/ .*/, "", $i); print "* `" $i "`"; }}' | egrep -v '(debhelper)'

* `automake`
* `autotools-dev`
* `libedit-dev`
* `libjemalloc-dev`
* `libncurses-dev`
* `libpcre3-dev`
* `libtool`
* `pkg-config`
* `python-docutils`
* `python-sphinx`
* `graphviz`

Build dependencies on Red Hat / CentOS
--------------------------------------

To build Varnish on a Red Hat or CentOS system you need the following
packages installed:

.. gawk '/^BuildRequires/ {print "* `" $2 "`"}' ../../../redhat/varnish.spec | sort | uniq | egrep -v '(systemd)'

* `autoconf`
* `automake`
* `jemalloc-devel`
* `libedit-devel`
* `libtool`
* `ncurses-devel`
* `pcre-devel`
* `pkgconfig`
* `python-docutils`
* `python-sphinx`
* `graphviz`

Build dependencies on a SmartOS Zone
------------------------------------

As of SmartOS pkgsrc 2015Q4, install the following packages::

	pkgin in autoconf automake libedit libtool ncurses \
		 pcre graphviz py27-sphinx python27 gmake gcc49 \
		 pkg-config

Optionally, to pull from a repository::

	pkgin in git

Compiling Varnish
-----------------

The configuration will need the dependencies above satisfied. Once that is
taken care of::

	cd varnish-cache
	sh autogen.sh
	sh configure
	make

The `configure` script takes some arguments, but more likely than not you can
forget about that for now, almost everything in Varnish can be tweaked with run
time parameters.

Before you install, you may want to run the test suite, make a cup of
tea while it runs, it usually takes a couple of minutes::

	make check

Don't worry if one or two tests fail. Some of the tests are a
bit too timing sensitive (Please tell us which so we can fix them). 
However, if a lot of them fail, and in particular if the `b00000.vtc` test
fails, something is horribly wrong. You will get nowhere without
figuring this one out.

Installing
----------

And finally, the true test of a brave heart: ``sudo make install``

Varnish will now be installed in ``/usr/local``. The ``varnishd`` binary is in
`/usr/local/sbin/varnishd`. To make sure that the necessary links and caches
of the most recent shared libraries are found, run ``sudo ldconfig``.

Next steps
==========

After successful installation you are ready to proceed to the :ref:`tutorial-index`.

This tutorial is written for installations from binary packages.
In practice, it means that some configurations are not in place for installations from source code.
For example, instead of calling ``service varnish start``, you start the varnish daemon manually by typing::

        varnishd -a :6081 -T localhost:6082 -b localhost:8080
