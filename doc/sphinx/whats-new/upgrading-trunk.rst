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

VCL types INTEGER and REAL now map respectively to Structured Fields integer
and decimal numbers. Numbers outside of the Structured Fields bounds are no
longer accepted by the VCL compiler and the various conversion functions from
vmod_std will fail the transaction for numbers out of bounds.

The scientific notation is no longer supported, for example ``12.34e+3`` must
be spelled out as ``12340`` instead.

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

Changes for developers
======================

Build
-----

Building from source requires autoconf 2.69 or newer and automake 1.13 or
newer. Neither are needed when building from a release archive since they
are already bootstrapped.

There is a new ``--enable-workspace-emulator`` configure flag to replace the
regular "packed allocation" workspace with a "sparse allocation" alternative.
Combined with the Address Sanitizer it can help VMOD authors find memory
handling issues like buffer overflows that could otherwise be missed on a
regular workspace.

``vdef.h``
----------

The ``vdef.h`` header is no longer self-contained, it includes ``stddef.h``.

Since it is the first header that should be included when working with Varnish
bindings, some definitions were promoted to ``vdef.h``:

- a fallback for the ``__has_feature()`` macro in its absence
- VRT macros for Structured Fields number limits
- ``struct txt`` and its companion macros (the macros require ``vas.h`` too)

This header is implicitly included by ``vrt.h`` and ``cache.h`` and should not
concern VMOD authors.

Workspace API
-------------

The deprecated functions ``WS_Front()`` and ``WS_Inside()`` are gone, they
were replaced by ``WS_Reservation()`` and ``WS_Allocated()``. For this reason
``WS_Assert_Allocated()`` was removed despite not being deprecated, since it
became redundant with ``assert(WS_Allocated(...))``. Accessing the workspace
front pointer only makes sense during a reservation, that's why ``WS_Front()``
was deprecated in a previous release.

It should no longer be needed to access ``struct ws`` fields directly, and
everything should be possible with the ``WS_*()`` functions. It even becomes
mandatory when the workspace emulator is enabled, the ``struct ws`` fields
have different semantics.

``STRING_LIST``
---------------

VMOD authors can no longer take ``STRING_LIST`` arguments in functions or
object methods. To work with string fragments, use ``VCL_STRANDS`` instead.

As a result the following symbols are gone:

- ``VRT_String()``
- ``VRT_StringList()``
- ``VRT_CollectString()``
- ``vrt_magic_string_end``

Functions that used to take a ``STRING_LIST`` in the form of a prototype
ending with ``const char *, ...`` now take ``const char *, VCL_STRANDS``:

- ``VRT_l_client_identity()``
- ``VRT_l_req_method()``
- ``VRT_l_req_url()``
- ``VRT_l_req_proto()``
- ``VRT_l_bereq_method()``
- ``VRT_l_bereq_url()``
- ``VRT_l_bereq_proto()``
- ``VRT_l_beresp_body()``
- ``VRT_l_beresp_proto()``
- ``VRT_l_beresp_reason()``
- ``VRT_l_beresp_storage_hint()``
- ``VRT_l_beresp_filters()``
- ``VRT_l_resp_body()``
- ``VRT_l_resp_proto()``
- ``VRT_l_resp_reason()``
- ``VRT_l_resp_filters()``

The ``VRT_SetHdr()`` function also used to take a ``STRING_LIST`` and now
takes a ``const char *, VCL_STRANDS`` too. But, in addition to this change,
it also no longer accepts the special ``vrt_magic_string_unset`` argument.

Instead, a new ``VRT_UnsetHdr()`` function was added.

The ``VRT_CollectStrands()`` function was renamed to ``VRT_STRANDS_string()``,
which was its original intended name.

Null sentinels
--------------

Two convenience sentinels ``vrt_null_strands`` and ``vrt_null_blob`` were
added to avoid ``NULL`` usage. ``VRT_blob()`` returns ``vrt_null_blob`` when
the source is null or the length is zero. The null blob has the type
``VRT_NULL_BLOB_TYPE``.

libvarnishapi
-------------

Deprecated functions ``VSB_new()`` and ``VSB_delete()`` were removed. Use
``VSB_init()`` and ``VSB_fini()`` for static buffers and ``VSB_new_auto()``
and ``VSB_destroy()`` for dynamic buffers.

Their removal resulted in bumping the soname to 3.0.0 for libvarnishapi.

libvarnish
----------

Other changes were made to libvarnish, those are only available to VMOD
authors since they are not exposed by libvarnishapi.

VNUM
''''

The ``VNUMpfx()`` function was replaced by ``SF_Parse_Number()`` that parses
both decimal and integer numbers from RFC8941. In addition there are new
``SF_Parse_Decimal()`` and ``SF_Parse_Integer()`` more specialized functions.

``VNUM_bytes_unit()`` returns an integer and no longer parses factional bytes.

New token parsers ``VNUM_uint()`` and ``VNUM_hex()`` were added.

The other VNUM functions rely on the new SF functions for parsing, with the
same limitations.

The following macros define the Structured Fields number bounds:

- ``VRT_INTEGER_MIN``
- ``VRT_INTEGER_MAX``
- ``VRT_DECIMAL_MIN``
- ``VRT_DECIMAL_MAX``

VRE
'''

The VRE API completely changed in preparation for the PCRE2 migration, in
order to funnel all PCRE usage in the Varnish source code through VRE.

Similarly to how parameters were renamed, the ``match_recursion`` field from
``struct vre_limits`` was renamed to ``depth``. It has otherwise the same
meaning and purpose.

Notable breaking changes:

- ``VRE_compile()`` signature changed
- ``VRE_exec()`` was replaced:
  - ``VRE_match()`` does simple matching
  - ``VRE_capture()`` captures matched groups in a ``txt`` array
  - ``VRE_sub()`` substitute matches with a replacement in a VSB
- ``VRE_error()`` prints an error message for all the functions above in a VSB
- ``VRE_export()`` packs a usable ``vre_t`` that can be persisted as a byte
  stream

An exported regular expression takes the form of a byte stream of a given size
that can be used as-is by the various matching functions. Care should be taken
to always maintain pointer alignment of an exported ``vre_t``.

The ``VRE_ERROR_NOMATCH`` symbol is now hard-linked like ``VRE_CASELESS``, and
``VRE_NOTEMPTY`` is no longer supported. There are no match options left in
the VRE facade but the ``VRE_match()``, ``VRE_capture()`` and ``VRE_sub()``
functions still take an ``options`` argument to keep the ability of allowing
match options in the future.

The ``VRE_ERROR_LEN`` gives a size that should be safe to avoid truncated
error messages in a static buffer.

To gain full access to PCRE2 features from a regular expression provided via
``vre_t`` a backend-specific ``vre_pcre2.h`` contains a ``VRE_unpack()``
function. This opens for example the door to ``pcre2_substitute()`` with the
PCRE2 substitution syntax and named capture groups as an alternative to VCL's
``regsub()`` syntax backed by ``VRE_sub()``.

Ideally, ``vre_pcre2.h`` will be the only breaking change next time we move
to a different regular expression engine. Hopefully not too soon.

*eof*
