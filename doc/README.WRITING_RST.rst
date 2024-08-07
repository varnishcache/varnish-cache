..
	Copyright 2015,2016,2019,2024 UPLEX - Nils Goroll Systemoptimierung
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

THINGS TO CONSIDER WHEN WRITING VARNISH RST DOCUMENTATION
=========================================================

Inline Markup
-------------

Please try to be consistent with inline markup and fix places which do
not follow the style:

* VCL language and other literals as ``literal``

* placeholders and emphasis as *emphasis*

* no `interpreted text` except where it actually *is* that

.. _Reference: http://docutils.sourceforge.net/docs/ref/rst/restructuredtext.html#character-level-inline-markup

  * exception: Links to manpages outside varnish as `man(section)`

* use code blocks for::

    Examples and

    other code

References are tricky
---------------------

To build html documentation, we want to create cross-document
cross-references using::

  :ref:`reference name`

Trouble is that ``rst2man`` and ``rst2pdf`` working on individual
files cannot parse `ref` roles to anything outside the current rst
file, so we need to differentiate link targets depending on the kind
of documentation:

* set link targets on the top of documents ending up in man-pages
  following the manpage naming scheme, e.g.::

    .. _varnishd(1):

* set link targets for important paragraphs following the scheme
  ref-`doc`-`section`, for instance::

    .. _ref-varnishd-opt_T:

  These can be referenced from other documents making up the html
  documentation, but not from stand-alone documents (like man-pages).

* in all documents which are used to create man-pages, add the
  following definition at the top::

    .. role:: ref(emphasis)

  This will allow the use of `ref` in a compatible manner, IF
  references follow the man-page naming scheme

* to be compatible both with ``sphinx`` and ``rst2man``, use `implicit
  link targets`_ in stand-alone documents, like this one creating
  `References are tricky`_::

    `References are tricky`_

.. _implicit link targets: http://docutils.sourceforge.net/docs/ref/rst/restructuredtext.html#implicit-hyperlink-targets

Document Structure
------------------

While RST supports a great deal of flexibility for adornments of
titles and section headers, we should adhere to a consistent style to
avoid breaking the document structure unintentially.

Within the Varnish-Cache project, we should use these characters:

* Over and underline ``=`` for document titles

* Over and underline ``-`` for document subtitles

* Underline ``=`` for sections

* Underline ``-`` for subsection

* Underline ``~`` for subsubsections

This is in line with the example in
https://docutils.sourceforge.io/docs/user/rst/quickstart.html#sections

HISTORY
=======

This README was initially started by Nils Goroll.

COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright 2015,2016,2019,2024 UPLEX - Nils Goroll
  Systemoptimierung
