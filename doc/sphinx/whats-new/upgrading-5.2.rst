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

Added ``req.hash`` and ``bereq.hash``, which contain the hash value
computed by Varnish for cache lookup in the current transaction, to
be used in client or backend context, respectively. Their data type
is BLOB, and they contain the raw binary hash.

You can use :ref:`vmod_blob(3)` to work with the hashes::

  import blob;

  sub vcl_backend_fetch {
      # Send the transaction hash to the backend as a hex string
      set bereq.http.Hash = blob.encode(HEX, blob=bereq.hash);
  }

  sub vcl_deliver {
      # Send the hash in a response header as a base64 string
      set resp.http.Hash = blob.encode(BASE64, blob=req.hash);
  }

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

* Changes for developers:

  * The VSM and VSC APIs for shared memory and statistics have
    changed, and may necessitate changes in client applications, see
    :ref:`whatsnew_vsm_vsc_5.2`.

  * Added the ``$ABI`` directive for VMOD vcc declarations, see
    :ref:`whatsnew_abi`.

  * There have been some minor changes in the VRT API, which may be
    used for VMODs and client apps, see :ref:`whatsnew_vrt_5.2`.

  * The VUT API (for Varnish UTilities), which facilitates the
    development of client apps, is now publicly available, see
    :ref:`whatsnew_vut_5.2`.

  * *XXX: anything else, such as sanitizer flags?*

  * *XXX: ...*

* *XXX: other changes in tools and infrastructure in and around
  Varnish ...*

  * *XXX: anything new about project tools, VTEST & GCOV, etc?*

  * *XXX: ...*
