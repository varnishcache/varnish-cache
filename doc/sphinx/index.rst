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

Welcome to the Varnish documentation!
=====================================

Introduction
------------

Varnish Cache is a web application accelerator also known as a caching HTTP reverse proxy. You install it in front of any server that speaks HTTP and configure it to cache the contents. Varnish Cache is really, really fast. It typically speeds up delivery with a factor of 300 - 1000x, depending on your architecture. 
It has its mission in front of a
web server and cache content and it makes your web site go faster.

To get started with Varnish-Cache we recommend that you read the installation guide
:ref:`install-index`. Once you have Varnish up and running we recommend that you go through
our tutorial - :ref:`tutorial-index`, and finally the :ref:`users_guide_index`.

If you need to find out how to use a specific Varnish tool, the
:ref:`reference-index` contains detailed documentation over the tools. Changes from previous versions are located in
the :ref:`whats-new-index` chapter. In closing, we have :ref:`phk`, a collection
of blog posts from Poul-Henning Kamp related to Varnish and HTTP.



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
==================

* :ref:`genindex`
* :ref:`search`
