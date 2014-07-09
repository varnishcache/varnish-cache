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
<http://www.varnish-cache.org/installation/redhat>`_ for more information.

Varnish is included in the `EPEL
<http://fedoraproject.org/wiki/EPEL>`_ repository, however due to
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
<http://www.varnish-cache.org/installation/debian>`_ or `Ubuntu
<http://www.varnish-cache.org/installation/ubuntu>`_.


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

Don't worry if one or two tests fail, some of the tests are a
bit too timing sensitive (Please tell us which so we can fix them.) but
if a lot of them fails, and in particular if the `b00000.vtc` test
fails, something is horribly wrong, and you will get nowhere without
figuring out what.

Installing
----------

And finally, the true test of a brave heart: ``sudo make install``

Varnish will now be installed in `/usr/local`. The `varnishd` binary is in
`/usr/local/sbin/varnishd` and its default configuration will be
`/usr/local/etc/varnish/default.vcl`.

After successful installation you are ready to proceed to the :ref:`tutorial-index`.

