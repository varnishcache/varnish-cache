.. _whatsnew_changes_8.0:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Changes in Varnish-Cache 8.0
%%%%%%%%%%%%%%%%%%%%%%%%%%%%

For information about updating your current Varnish deployment to the
new version, see :ref:`whatsnew_upgrading_8.0`.

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
uncacheable (or hit-for-miss) by the built-in VCL. It is accessible in VCL as
the ``param.uncacheable_ttl`` variable.

The ``http_req_overflow_status`` parameter can now also be set to 500.

The default value for ``ban_any_variant`` is now ``0``. This means that during a
lookup, only the matching variants of an object will be evaluated against the
ban list.

As a side effect, variants that are rarely requested may never get a chance to
be tested against ``req`` based bans, which can lead to an accumulation of bans
over time. In such cases, it is recommended to set ``ban_any_variant`` to a
higher value.

Other changes in varnishd
~~~~~~~~~~~~~~~~~~~~~~~~~

The ``Content-Length`` response header is now also sent in response to all
``HEAD`` requests.

``builtin.vcl`` has been updated to return a synthetic 501 response and close
the connection when receiving requests with an unknown/unsupported http method
instead of piping them.

Handling of request coalescing using the `waitinglist` mechanism has been
changed fundamentally in order to allow for all requests waiting in parallel to
handle a newly arriving cache entry object as successfully revalidated - in
other words, cases where a response with a ttl and grace value of 0 seconds
still serves multiple client requests.

A ``stop`` command to the ``varnishd`` process now explicitly waits for all VCL
references to be returned, which is the same as waiting for all ongoing
transactions to complete. There is currently no timeout. If this new behavior is
unwanted, the worker process can still be terminated externally.

Request body read failures now result in a ``400`` response status.

Changes to VCL
==============

VCL variables
~~~~~~~~~~~~~

Some runtime parameters can now be accessed from VCL as ``param.<param_name>``.
See ``VCL-VARIABLES(7)`` for the list of available parameters.

Other changes to VCL
~~~~~~~~~~~~~~~~~~~~

VCL control has been added over the logic to handle ``304 Not Modified``
responses to backend requests. The subroutine ``vcl_backend_refresh`` is now
getting called with ``304`` response objects as ``beresp`` and the stale object
to potentially re-use as ``obj_stale``. From this subroutine, ``return(merge)``
will invoke the header merging and re-use of the stale object body which so far
was the only option. But besides the usual options to fail, abandon, retry or
return an error, it now also offers the options to return the stale object as-is
(``return(obj_stale)``) or to return whatever headers are received or created in
``beresp.http`` using ``return(beresp)``. The latter option allows for almost
full control over the response headers to use with the stale object, with the
exception of the ``Content-Length``, ``Content-Encoding``, ``Etag`` and
``Last-Modified`` headers, which are copied from the stale object for
correctness.


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

The already deprecated VMOD function ``cookie.format_rfc1123()`` is now removed.
It had been renamed to ``cookie.format_date()``.

VUTs
====

VUTs now print backtraces to syslog after a crash.

varnishlog
==========

The format of logs emitted under the ``ESI_xmlerror`` tag has been changed
slightly with a colon added after the ``ERR`` and ``WARN`` prefixes. This allows
use of prefix-matching with vsl clients, for example using
``%{VSL:ESI_xmlerror:WARN}x``.

varnishadm
==========

The new ban expression variable `obj.last_hit` allows to remove objects from
cache which have not been accessed for a given amount of time. This is
particularly useful to get rid of request bans by removing all objects which
have not been touched since the request ban.

varnishstat
===========

New VSC counters for connection pools have been added:

- ``VCP.ref_hit`` counts the number of times an existing connection pool was
    found while creating a backend.
- ``VCP.ref_miss`` counts the number of times an existing connection pool was
    not found while creating a backend.

New counters ``transit_stored`` and ``transit_buffered`` have been added. The
former is the number of bytes stored in cache for uncachable body data, and the
latter is the number of bytes of body data for which the ``transit_buffer``
limitation has been used.

``varnishstat`` will automatically switch to ``-1`` output if ``stdout`` isn't a
terminal (allowing ``varnishstat | grep MAIN``). A new ``-c`` switch has been
added to force curses (interactive terminal) mode.

varnishtest
===========

``varnishtest`` now prints a backtrace to stderr after a crash.

The bundled varnishtest sources have now been replaced with the separate VTest2
repository.

varnishncsa
===========

``varnishncsa`` regained the ability to log headers set from VCL through the
``%{X[:first|last]}i`` and ``%{X[:first|last]}o`` formats. ``:first`` means
that the value of the header when it is first seen in a VSL transaction is
logged, while ``:last`` means that the final one is logged.
The default behavior is unchanged when neither ``:first`` nor ``:last`` is
specified.

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

``struct strands`` and ``struct vrt_blob`` have become mini objects.

The ``vcountof()`` utility macro has been added to ``vdef.h``

*eof*
