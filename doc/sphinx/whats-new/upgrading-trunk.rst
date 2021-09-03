..
	Copyright 2021 Varnish Software
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

**Note: This is a working document for a future release, with running
updates for changes in the development branch. For changes in the
released versions of Varnish, see:** :ref:`whats-new-index`

.. _whatsnew_upgrading_CURRENT:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish **$NEXT_RELEASE**
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

PCRE2
=====

The migration from PCRE to PCRE2 led to many changes, starting with a
change of build dependencies. See the current installation notes for
package dependencies on various platforms.

Previously the Just In Time (jit) compilation of regular expressions was
always enabled at run time if it was present during the build. From now
on jit compilation is enabled by default, but can be disabled with the
``--disable-pcre2-jit`` configure option. Once enabled, jit compilation
is merely attempted and failures are ignored since they are not essential.

The new ``varnishd`` parameter ``pcre2_jit_compilation`` controls whether
jit compilation should be attempted and has no effect if jit support was
disabled at configure time.

The former parameters ``pcre_match_limit`` and ``pcre_match_limit_recursion``
were renamed to ``pcre2_match_limit`` and ``pcre2_depth_limit``. With older
PCRE2 libraries, it is possible to see the depth limit being referred to as
recursion limit in error messages.

The syntax of regular expression should be the same, but it is possible to
run into subtle differences. We are aware one such difference, PCRE2 fails
the compilation of unknown escape sequences. For example PCRE interprets
``"\T"`` as ``"T"`` and ignores the escape character, but PCRE2 sees it as
a syntax error. The solution is to simply use ``"T"`` and in general remove
all spurious escape characters.

While PCRE2 can capture named groups and has its own substitution syntax
where captured groups can be referred to by position with ``$<n>`` or even
by name. The substitution syntax for VCL's ``regsub()`` remains the same and
captured groups still require the ``\<n>`` syntax where ``\1`` refers to
the first group.

For this reason, there shouldn't be changes required to existing VCL, ban
expressions, VSL queries, or anything working with regular expression in
Varnish, except maybe where PCRE2 seems to be stricter and refuses invalid
escape sequences.

VMOD authors manipulating ``VCL_REGEX`` arguments should not be affected by
this migration if they only use the VRT API. However, the underlying VRE API
was substantially changed and the new revision of VRE allowed to cover all
the Varnish use cases so that ``libvarnish`` is now the only binary linking
*directly* to ``libpcre2-8``.

The migration implies that bans persisted in the deprecated persistent storage
are no longer compatible and a new deprecated persistent storage should be
rebuilt from scratch.

Structured Fields numbers
=========================

TODO

Memory footprint
================

In order to lower the likelihood of flushing the logs of a single task more
than once, the default value for ``vsl_buffer`` was increased to 16kB. This
should generally result in better performance with tools like ``varnishlog``
or ``varnishncsa`` except for ``raw`` grouping.

To accommodate this extra workspace consumption and add even more headroom
on top of it, ``workspace_client`` and ``workspace_backend`` both increased
to 96kB by default.

The PCRE2 jit compiler produces code that consumes more stack, so the default
value of ``thread_pool_stack`` was increased to 80kB, and to 64kB on 32bit
systems.

If you are relying on default values, this will result in an increase of
virtual memory consumption proportional to the number of concurrent client
requests and backend fetches being processed. This memory is not accounted
for in the storage limits that can be applied.

To address a potential head of line blocking scenario with HTTP/2, request
bodies are now buffered between the HTTP/2 session (stream 0) and the client
request. This is allocated on storage, controlled by the ``h2_rxbuf_storage``
parameter and comes in addition to the existing buffering between a client
request and a backend fetch also allocated on storage. The new buffer size
depends on ``h2_initial_window_size`` that has a new default value of 65535B
to avoid having streams with negative windows.

Range requests
==============

Varnish only supports bytes units for range requests and always stripped
``Accept-Range`` headers coming from the backend. This is no longer the case
for pass transactions.

To find out whether an ``Accept-Range`` header came from the backend, the
``obj.uncacheable`` in ``vcl_deliver`` indicates whether this was a pass
transaction.

When ``http_range_support`` is on, a consistency check is added to ensure
the backend doesn't act as a bad gateway. If an unexpected ``Content-Range``
header is received, or if it doesn't match the client's ``Range`` header,
it is considered an error and a 503 response is generated instead.

If your backend adds spurious ``Content-Range`` headers that you can assess
are safe to ignore, you can amend the response in VCL::

    sub vcl_backend_response {
        if (!bereq.http.range) {
            unset beresp.http.content-range;
        }
    }

When a consistency check fails, an error is logged with the specific range
problem encountered.

ACL
===

The ``acl`` keyword in VCL now supports bit flags:

- ``log``
- ``pedantic`` (enabled by default)
- ``table``

The global parameter ``vcc_acl_pedantic`` (off by default) was removed, and
as a result ACLs are now pedantic by default. TODO: reference to manual.

They are also quiet by default, the following ACL declarations are
equivalent::

    acl <name> { ... }
    acl <name> -log +pedantic -table { ... }

This means that the entry an ACL matched is no longer logged as ``VCL_acl`` by
default.

To restore the previous default behavior, declare your ACL like this::

    acl <name> +log -pedantic { ... }

ACLs are optimized for runtime performance by default, which can increase
significantly the VCL compilation time with very large ACLs. The ``table``
flag improves compilation time at the expense of runtime performance.

Changes for VMOD authors
========================

TODO

*eof*
