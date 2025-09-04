**Note: This is a working document for a future release, with running
updates for changes in the development branch. For changes in the
released versions of Varnish, see:** :ref:`whats-new-index`

.. _whatsnew_changes_CURRENT:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Changes in Varnish **$NEXT_RELEASE**
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

For information about updating your current Varnish deployment to the
new version, see :ref:`whatsnew_upgrading_CURRENT`.

A more detailed and technical account of changes in Varnish, with
links to issues that have been fixed and pull requests that have been
merged, may be found in the `change log`_.

.. _change log: https://github.com/varnishcache/varnish-cache/blob/master/doc/changes.rst

varnishd
========

Parameters
~~~~~~~~~~


Read only parameter can no longer be set through an alias.

Deprecated aliases for parameters can no longer be set read only, it should
instead be done directly on the parameters they point to.

A new parameter ``uncacheable_ttl`` defines the TTL of objects marked as
uncacheable (or hit-for-miss) by the built-in VCL. It is accessible in VCL
as the ``param.uncacheable_ttl`` variable.

`http_req_overflow_status` can now also be set to 500.

Other changes in varnishd
~~~~~~~~~~~~~~~~~~~~~~~~~

Changes to VCL
==============

VCL variables
~~~~~~~~~~~~~

Runtime parameters can now be accessed from VCL through:
``param.<param_name>``. See ``VCL-VARIABLES(7)`` for the list of available
parameters.

Other changes to VCL
~~~~~~~~~~~~~~~~~~~~

VMODs
=====

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

VUTs
====

VUTs now print backtraces to syslog after a crash.

varnishlog
==========

**XXX changes concerning varnishlog(1) and/or vsl(7)**

varnishadm
==========

New ban expression variable `obj.last_hit` allows to remove objects from
cache which have not been accessed for a given amount of time. This is
particularly useful to get rid of request bans by removing all objects which
have not been touched since the request ban.

varnishstat
===========

New VSC counters for connection pools:

- ``VCP.ref_hit`` counts the number of times an existing connection pool was
    found while creating a backend.
- ``VCP.ref_miss`` counts the number of times an existing connection pool was
    not found while creating a backend.

varnishtest
===========

``varnishtest`` now prints a backtrace to stderr after a crash.

Changes for developers and VMOD authors
=======================================

`hdr_t` type is now a structured type but keeps the same memory layout as
before.

``VRT_VSC_Alloc()`` was renamed to ``VRT_VSC_Allocv()`` and a new version of
``VRT_VSC_Alloc()`` that takes a ``va_list`` argument was reintroduced. This
makes it consistent with our naming conventions.

vmod authors can now specify C names for function/method arguments like follows:

  [BOOL bool:boolean]

This is useful to avoid name clashes with keywords reserved by the language.

*eof*
