
Varnish Administrator Documentation
===================================

Varnish Cache is a web application accelerator also known as a caching HTTP
reverse proxy. You install it in front of any server that speaks HTTP and
configure it to cache the contents. Varnish Cache is really, really fast. It
typically speeds up delivery with a factor of 300 - 1000x, depending on your
architecture.

To get started with Varnish-Cache we recommend that you read the installation
guide :ref:`install-index`. Once you have Varnish up and running we recommend
that you go through our tutorial - :ref:`tutorial-index`, and finally the
:ref:`users-guide-index`.

If you need to find out how to use a specific Varnish tool, the
:ref:`reference-index` contains detailed documentation over the tools. Changes
from previous versions are located in the :ref:`whats-new-index` chapter. In
closing, we have :ref:`phk`, a collection of blog posts from Poul-Henning Kamp
related to Varnish and HTTP.


Conventions used in this manual include:

  ``service varnish restart``
    A command you can run, or a shortkey you can press. Used either in the
    terminal or after starting one of the tools.

  `/usr/local/`, `varnishadm`, `sess_timeout`
    A utility, Varnish configurable parameter or path.

  http://www.varnish-cache.org/
    A hyperlink.

Longer listings like example command output and VCL look like this::

    $ /opt/varnish/sbin/varnishd -V
    varnishd (varnish-4.0.0-tp1 revision ddd00e1)
    Copyright (c) 2006 Verdens Gang AS
    Copyright (c) 2006-2011 Varnish Software AS


.. For maintainers:
.. * always write Varnish with a capital V: Varnish, Varnish Cache.
.. * Write Varnish tools as their executable name: `varnishd`, `varnishadm`.
.. * if part of a command actually runable by the reader, use double backticks:
..   ``varnishd -f foo.c``
.. * wrap lines at 80 characters, ident with 4 spaces. No tabs, please.
.. We use the following header indicators
.. For titles:

.. H1
.. %%%%%

.. Title
.. %%%%%

.. H2 - H5
.. ======================
.. ----------------------
.. ~~~~~~~~~~~~~~~~~~~~~~
.. ......................


.. toctree::
    :maxdepth: 1

    installation/index.rst
    tutorial/index.rst
    users-guide/index.rst
    reference/index.rst
    whats-new/index.rst
    phk/index.rst
    glossary/index.rst



Indices and tables
------------------

* :ref:`genindex`
* :ref:`search`
