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

Cloud images of Varnish
=======================

.. toctree::
	:maxdepth: 2

	cloud_debian
	cloud_redhat
	cloud_ubuntu


Compiling Varnish from source
=============================

If there are no binary packages available for your system, or if you
want to compile Varnish from source for other reasons:

.. toctree::
	:maxdepth: 2

	install_source

Next steps
==========

After successful installation you are ready to proceed to the :ref:`tutorial-index`.

This tutorial is written for installations from binary packages.
In practice, it means that some configurations are not in place for installations from source code.
For example, instead of calling ``service varnish start``, you start the varnish daemon manually by typing::

        varnishd -a :6081 -T localhost:6082 -b localhost:8080
