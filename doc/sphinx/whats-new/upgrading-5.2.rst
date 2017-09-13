.. _whatsnew_upgrading_5.2:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 5.2
%%%%%%%%%%%%%%%%%%%%%%%%

Varnish statistics and logging
==============================

There are extensive changes under the hood with respect to statistics
counters, but these should all be transparent at the user-level.

varnishd parameters
===================

* *XXX: -p params that are new, modified, deprecated or removed*

* *XXX: use rst refs to keep it short*

* *XXX: ...*

Changes to VCL
==============

*XXX: intro paragraph*

*XXX: emphasize what most likely needs to be done, if anything,*
*to migrate from 5.1 to 5.2*

*XXX: ... or reassure that you probably don't have to do anything*
*to migrate from 5.1 to 5.2*

XXX: headline changes ...
~~~~~~~~~~~~~~~~~~~~~~~~~

*XXX: the most important changes or additions first*

``req.hash`` and ``bereq.hash``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

*XXX: about {be}req.hash, mention data type BLOB, advise about*
*:ref:`vmod_blob(3)`*

XXX: vcl_sub_XXX ...
~~~~~~~~~~~~~~~~~~~~

*XXX: list changes by VCL sub*

XXX: more VCL changes ...
~~~~~~~~~~~~~~~~~~~~~~~~~

*XXX: any more details and new features that VCL authors have to know*

vmod_std
~~~~~~~~

* *XXX: any changes in VMOD std?*

New VMODs in the standard distribution
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

See :ref:`vmod_blob(3)`, :ref:`vmod_purge(3)`, :ref:`vmod_vtc(3)` and
:ref:`whatsnew_new_vmods`.

Other changes
=============

* ``varnishstat(1)``:

  * *XXX: changes due to new VSC/VSM*

  * *XXX: ...*

* ``varnishlog(1)``:

  * *XXX: changes due to new VSC/VSM*

  * *XXX: ...*

* ``varnishtest(1)`` and ``vtc(7)``:

  * *XXX: changes in test code*

  * *XXX: for example due to VMOD vtc*

  * *XXX: ...*

* *XXX: any other changes in the standard VUT tools*

  * *XXX: ...*

* *XXX: changes in VRT that may affect VMOD authors*

  * *XXX: ...*

* *XXX: changes for developers?*

  * *XXX: such as sanitizer flags?*

  * *XXX: ...*

* *XXX: other changes in tools and infrastructure in and around
  Varnish ...*

  * *XXX: anything new about project tools, VTEST & GCOV, etc?*

  * *XXX: ...*
