
.. _whatsnew_upgrading_8.0:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish-Cache 8.0
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

This document only lists breaking changes that you should be aware of when
upgrading from Varnish-Cache 7.x to 8.0. For a complete list of changes,
please refer to the `change log`_ or :ref:`whatsnew_changes_8.0`.

.. _change log: https://github.com/varnishcache/varnish-cache/blob/master/doc/changes.rst

vmod_std changes
================

The VMOD functions ``std.real2integer()``, ``std.real2time()``,
``std.time2integer()`` and ``std.time2real()`` have been removed. They had
been marked deprecated since Varnish Cache release 6.2.0 (2019-03-15).

The plug-in replacements for these functions are:

 ``std.real2integer()``::

        std.integer(real=std.round(...), fallback=...)

 ``std.real2time()``::

        std.time(real=std.round(...), fallback=...)

 ``std.time2integer()``::

        std.integer(time=..., fallback=...)

 ``std.time2real()``::

        std.real(time=..., fallback=...)

vmod_cookie changes
===================

The already deprecated VMOD function ``cookie.format_rfc1123()`` is now removed.
It had been renamed to ``cookie.format_date()``.

Upgrade notes for VMOD developers
=================================

``VRT_VSC_Alloc()`` was renamed to ``VRT_VSC_Allocv()`` and a new version of
``VRT_VSC_Alloc()`` that takes a ``va_list`` argument was reintroduced. This
makes it consistent with our naming conventions.

``struct strands`` and ``struct vrt_blob`` have become mini objects. Both are
usually created through existing VRT functions, but where they are managed
specifically, they  should  now be initialized / allocated with:

* ``INIT_OBJ(strands, STRANDS_MAGIC)`` / ``ALLOC_OBJ(strands, STRANDS_MAGIC)``
* ``INIT_OBJ(blob, VMOD_BLOB_MAGIC)`` / ``ALLOC_OBJ(blo, VMOD_BLOB_MAGIC)``

The already deprecated functions ``VRT_{Add,Remove}_{VDP,VFP}`` have been
removed from VRT. They had already been replaced by ``VRT_AddFilter()`` and
``VRT_RemoveFilter()``.

*eof*
