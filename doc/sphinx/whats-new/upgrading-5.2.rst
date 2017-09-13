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

The :ref:`ref_param_cli_buffer` parameter is deprecated and
ignored. Memory for the CLI command buffer is now dynamically
allocated.

We have updated the documentation for :ref:`ref_param_send_timeout`,
:ref:`ref_param_idle_send_timeout`, :ref:`ref_param_timeout_idle` and
:ref:`ref_param_ban_cutoff`.

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

Added :ref:`func_file_exists`.

New VMODs in the standard distribution
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

See :ref:`vmod_blob(3)`, :ref:`vmod_purge(3)` and
:ref:`vmod_vtc(3)`. See :ref:`whatsnew_new_vmods`.

Other changes
=============

* VSL

The ``Hit``, ``HitMiss`` and ``HitPass`` log records grew an additional
field with the remaining TTL of the object at the time of the lookup.
While this should greatly help troubleshooting, this might break tools
relying on those records to get the VXID of the object hit during lookup.

Instead of using ``Hit``, such tools should now use ``Hit[1]``, and the
same applies to ``HitMiss`` and ``HitPass``.

The ``Hit`` record also grew two more fields for the grace and keep periods.
This should again be useful for troubleshooting.

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

* *XXX: changes for developers?*

  * *XXX: such as sanitizer flags?*

  * *XXX: ...*

* *XXX: other changes in tools and infrastructure in and around
  Varnish ...*

  * *XXX: anything new about project tools, VTEST & GCOV, etc?*

  * *XXX: ...*
