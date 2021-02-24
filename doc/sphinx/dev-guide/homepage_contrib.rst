..
	Copyright (c) 2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of licens

.. _homepage_contrib:

How to contribute content to varnish-cache.org
==============================================

This is where we walk you through the mechanics of adding content to
varnish-cache.org (see phk's note :ref:`homepage_dogfood` for an
insight into the innards of site).

Git Repository
--------------

The web site contents live in github at:

https://github.com/varnishcache/homepage

To offer your own contribution, fork the project and send us a pull
request.

Sphinx and RST
--------------

The web site sources are written in `RST
<http://docutils.sourceforge.net/rst.html>`_ -- reStructuredText, the
documentation format originally conceived for Python (and also used in
the Varnish distribution, as well as for formatting VMOD
docs). `Sphinx <http://www.sphinx-doc.org/>`_ is used to render web
pages from the RST sources.

So you'll need to `learn markup with RST and Sphinx
<http://www.sphinx-doc.org/en/stable/markup/index.html>`_;  and you
will need to `install Sphinx <http://www.sphinx-doc.org/en/stable/install.html>`_ to test the rendering on your local system.

Makefile
--------

Generation of web contents from the sources is driven by the ``Makefile``
in the ``R1`` directory of the repo::

  $ cd R1
  $ make help
  Please use `make <target>' where <target> is one of
  html       to make standalone HTML files
  dirhtml    to make HTML files named index.html in directories
  singlehtml to make a single large HTML file
  pickle     to make pickle files
  json       to make JSON files
  htmlhelp   to make HTML files and a HTML help project
  qthelp     to make HTML files and a qthelp project
  applehelp  to make an Apple Help Book
  devhelp    to make HTML files and a Devhelp project
  epub       to make an epub
  latex      to make LaTeX files, you can set PAPER=a4 or PAPER=letter
  latexpdf   to make LaTeX files and run them through pdflatex
  latexpdfja to make LaTeX files and run them through platex/dvipdfmx
  text       to make text files
  man        to make manual pages
  texinfo    to make Texinfo files
  info       to make Texinfo files and run them through makeinfo
  gettext    to make PO message catalogs
  changes    to make an overview of all changed/added/deprecated items
  xml        to make Docutils-native XML files
  pseudoxml  to make pseudoxml-XML files for display purposes
  linkcheck  to check all external links for integrity
  doctest    to run all doctests embedded in the documentation (if enabled)
  coverage   to run coverage check of the documentation (if enabled)

Most of the time, you'll just need ``make html`` to test the rendering
of your contribution.

alabaster theme
---------------

We use the `alabaster theme <https://pypi.python.org/pypi/alabaster>`_,
which you may need to add to your local Python installation::

  $ sudo pip install alabaster

We have found that you may need to link the alabaster package install
directory to the directory where Sphinx expects to find themes. For
example (on my machine), alabaster was installed into::

  /usr/local/lib/python2.7/dist-packages/alabaster

And Sphinx expects to find themes in::

  /usr/share/sphinx/themes

So to get the make targets to run successfully::

  $ cd /usr/share/sphinx/themes
  $ ln -s /usr/local/lib/python2.7/dist-packages/alabaster

Test the rendering
------------------

Now you can edit contents in the website repo, and test the rendering
by calling make targets in the ``R1`` directory::

  $ cd $REPO/R1
  $ make html
  sphinx-build -b html -d build/doctrees   source build/html
  Running Sphinx v1.2.3
  loading pickled environment... done
  building [html]: targets for 1 source files that are out of date
  updating environment: 0 added, 1 changed, 0 removed
  reading sources... [100%] tips/contribdoc/index
  looking for now-outdated files... none found
  pickling environment... done
  checking consistency... done
  preparing documents... done
  writing output... [100%] tips/index
  writing additional files... genindex search
  copying static files... done
  copying extra files... done
  dumping search index... done
  dumping object inventory... done
  build succeeded.

After a successful build, the newly rendered contents are saved in the
``R1/source/build`` directory, so you can have a look with your
browser.

Send us a pull request
----------------------

When you have your contribution building successfully, send us a PR,
we'll be happy to hear from you!
