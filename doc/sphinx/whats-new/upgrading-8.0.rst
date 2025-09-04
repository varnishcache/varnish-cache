**Note: This is a working document for a future release, with running
updates for changes in the development branch. For changes in the
released versions of Varnish, see:** :ref:`whats-new-index`

.. _whatsnew_upgrading_CURRENT:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish **$NEXT_RELEASE**
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

vmod_std changes:
=================

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


Upgrade notes for VMOD developers
=================================

``VRT_VSC_Alloc()`` was renamed to ``VRT_VSC_Allocv()`` and a new version of
``VRT_VSC_Alloc()`` that takes a ``va_list`` argument was reintroduced. This
makes it consistent with our naming conventions.

*eof*
