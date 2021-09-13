..
	Copyright 2021 Varnish Software
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

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

PCRE2
=====

One major change for this release is the migration of the regular expression
engine from PCRE to PCRE2. This change should be mostly transparent anywhere
regular expressions are used, like VCL, ban expressions, VSL queries etc.

There were some parameters changes, see the upgrade notes for more details.

RFC8941 - Structured Fields
===========================

It will come as no surprise to VCL writers that HTTP headers use what can
charitably be described as "heterogenous syntax".

In 2016, on the train back from the HTTP Workshop in Stockholm, and
in response to proposals to allow JSON in HTTP headers, I started an effort
which culminated with the publication of `RFC 8941 Structured Fields`_.

The syntax in Structured Fields is distilled from current standardized headers,
which means that the vast majority of existing HTTP headers are already
covered.
There are unfortunate exceptions, most notably the Cookie headers.

Starting with this release, we are gently migrating VCL towards the
Structured Field semantics.

The first change is that it is now possible to include BLOBs in VCL,
by using the RFC 8941 syntax of::

	':' + <base64> + ':'

The second and likely more significant change is numbers in VCL
now conform to RFC8941 as well:  Up to 15 digits and at most 3
decimal places, and "scientific notation" is no longer allowed.

(These restrictions were chosen after much careful deliberation, to
ensure that no overflows would occur, even when HTTP headers are
processed in languages where numbers are represented by IEEE-754
64 binary floating point variables,)

.. _RFC 8941 Structured Fields: https://www.rfc-editor.org/rfc/rfc8941.html

varnishd
========

Parameters
~~~~~~~~~~

There were changes to the parameters:

- new ``pcre2_jit_compilation`` boolean defaulting to on
- the default value increased to 16kB for ``vsl_buffer``
- the default value increased to 96kB for ``workspace_client``
- the default value increased to 96kB for ``workspace_backend``
- the minimum value increased to 384B for ``workspace_session``
- the minimum value increased to 65535B for ``h2_initial_window_size``
- the default value increased to 80kB for ``thread_pool_stack``
- the default value increased to 64kB for ``thread_pool_stack`` on 32bit
  systems
- ``vcc_acl_pedantic`` was removed, see upgrade notes for more details.
- ``pcre_match_limit`` was renamed to ``pcre2_match_limit``
- ``pcre_match_limit_recursion`` was renamed to ``pcre2_depth_limit``
- new ``h2_rxbuf_storage`` defaulting to ``Transient``, see upgrade notes for
  more details.

Other changes in varnishd
~~~~~~~~~~~~~~~~~~~~~~~~~

For pass transactions, ``varnishd`` no longer strips ``Range`` headers from
client requests or ``Accept-Range`` and ``Content-Range`` headers from backend
responses to allow partial delivery directly from the backend.

When ``http_range_support`` is on (the default) a consistency check is
performed on the backend response and malformed or inconsistent responses
are treated as fetch errors.

There is a new buffer for HTTP/2 request bodies allocated from storage.

See upgrade notes for more details on both topics.

Changes to VCL
==============

VCL variables
~~~~~~~~~~~~~

A new ``req.hash_ignore_vary`` flag allows to skip vary header checks during a
lookup. This can be useful when only the freshness of a resource is relevant,
and not a slight difference in representation.

For interoperability purposes, it is now possible to quote header names that
aren't valid VCL symbols, but valid HTTP header names, for example::

    req.http."dotted.name"

This is rarely observed and should only be needed to better integrate with the
specific needs of certain clients or servers.

Some global VCL symbols can be referenced before their declaration, this was
extended to all global VCL symbols for the following keywords:

- ``acl``
- ``backend``
- ``probe``
- ``sub``

Consider the following example::

    sub vcl_recv {
        set req.backend_hint = b;
    }

    backend b {
        .host = "example.org";
    }

It used to fail the VCL compilation with "Symbol not found: 'b'" in
``vcl_recv``, and is now supported.

Bit flags
~~~~~~~~~

There is a new bit flag syntax for certain VCL keywords::

    keyword +flag -other ...

Similarly to bit flag ``varnishd`` parameters ``debug``, ``feature`` and
``vsl_mask``, a ``+`` prefix means that a flag is raised and a ``-`` prefix
that a flag is cleared.

The ``acl`` keyword supports the following flags:

- ``log``
- ``pedantic`` (enabled by default)
- ``table``

For example::

    acl <name> +log -pedantic { ... }

The ``include`` keyword supports a ``glob`` flag.

For example::

    include +glob "example.org/*.vcl";

See upgrade notes for more details.

VMODs
=====

New ``BASE64CF`` encoding scheme in ``vmod_blob``. It is similar to
``BASE64URL``, with the following changes to ``BASE64``:

- ``+`` replaced with ``-``
- ``/`` replaced with ``~``
- ``_`` as the padding character

It is used by a certain CDN provider who also inspired the name.

varnishlog
==========

If a cache hit occurs on a streaming object, an object that is still being
fetched, ``Hit`` records contain progress of the fetch task. This should help
troubleshooting when cache hits appear to be slow, whether or not the backend
is still serving the response.

By default ``VCL_acl`` records are no longer emitted. They can be brought back
by adding a ``+log`` flag to the ACL  declaration.

varnishncsa
===========

New ``%{...}t`` time formats:

- ``sec``
- ``msec``
- ``usec``
- ``msec_frac``
- ``usec_frac``

See the varnishncsa manual for more information.

varnishadm
==========

The ``-t`` option sets up the timeout for both attaching to a running
``varnishd`` instance and individual commands sent to that instance.

Command completion should be more accurate in interactive mode.

varnishtest
===========

Test cases should be generally more reactive, whether it is detecting
a ``varnishd`` startup failure, waiting for ``varnishd`` to stop, or
when fail tests and there are barriers waiting for a synchronization.

Clients and servers can have up to 64 headers in requests and responses.

The ``feature`` command allows to skip gracefully test cases that are
missing specific requirements. It is now possible to skip a test based on
the presence of a feature.

For example, for test cases targeting 32bit environment with a working DNS
setup::

    feature dns !64bit

There are new feature checks:

- ``coverage``
- ``asan``
- ``msan``
- ``tsan``
- ``ubsan``
- ``sanitizer``
- ``workspace_emulator``

The undocumented ``pcre_jit`` feature check is gone.

There is a new ``tunnel`` command that acts as a proxy between two peers. A
tunnel can pause and control how much data goes in each direction, and can
be used to trigger socket timeouts, possibly in the middle of protocol frames,
without having to change how the peers are implemented.

There is a new dynamic macro ``${string,repeat,<uint>,<string>}`` to avoid
very long lines or potential mistakes when maintained by hand. For example,
the two following strings are equivalent::

    "AAA"
    "${string,repeat,3,A}"

There were also various improvements to HTTP/2 testing, and more should be
expected.

Changes for developers and VMOD authors
=======================================

Varnish now comes with a second workspace implementation called the workspace
emulator. It needs to be enabled during the build with the configure flag
``--enable-workspace-emulator``.

The workspace emulator performs sparse allocations and can help VMOD authors
interested in fuzzing, especially when the Address Sanitizer is enabled as
well.

In order to make the emulator possible, some adjustments were needed for the
workspace API. Deprecated functions ``WS_Front()`` and ``WS_Inside()`` were
removed independently of the emulator.

The ``STRING_LIST`` type is gone in favor of ``STRANDS``. All the VRT symbols
related to ``STRING_LIST`` are either gone or changed.

Convenience constants ``vrt_null_strands`` and ``vrt_null_blob`` were added.

The migration to PCRE2 also brought many changes to the VRE API. The VRT
functions handling ``REGEX`` arguments didn't change.

The VNUM API also changed substantially for structured field number parsing.

The deprecated functions ``VSB_new()`` and ``VSB_delete()`` were removed.

See upgrade notes for more information.

**XXX changes concerning VRT, the public APIs, source code organization,
builds etc.**

*eof*
