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

A new kind of parameters exists: deprecated aliases. Their documentation is
minimal, mainly referring to the actual symbols they alias. They are not
listed in the CLI, unless referred to explicitly.

There is no deprecated alias yet, but some are already planned for future
releases. Alias parameters have next to no overhead when used directly.

The deprecated ``vsm_space`` parameter was removed.

A new ``cc_warnings`` parameter contains a subset of the compiler flags
extracted from ``cc_command``, which in turn grew new expansions:

- ``%d``: the raw default ``cc_command``
- ``%D``: the expanded default ``cc_command``
- ``%w``: the ``cc_warnings`` parameter
- ``%n``: the working directory (``-n`` option)

This should facilitate the creation of wrapper scripts around VCL compilation.

There is a new ``experimental`` parameter that is identical to the ``feature``
parameter, except that it guards features that may not be considered complete
or stable. An experimental feature may be promoted to a regular feature or
dropped without being considered a breaking change.

Command line options
~~~~~~~~~~~~~~~~~~~~

The deprecated sub-argument of the ``-l`` option was removed, it is now a
shorthand for the ``vsl_space`` parameter only.

The ``-T``, ``-M`` and ``-P`` command line options can be used multiple times,
instead of retaining only the last occurrence.

When there is no active VCL, the first loaded VCL was always implicitly used
too. This is now only true for VCLs loaded with either the ``-f`` or ``-b``
options, since they imply a ``vcl.use``. VCL loaded through the Varnish CLI
(``vcl.load`` or ``vcl.inline``) via a CLI script loaded through the ``-I``
command line option require an explicit ``vcl.use``.

Other changes in varnishd
~~~~~~~~~~~~~~~~~~~~~~~~~

ESI includes now support the ``onerror="continue"`` attribute. However, in
order to take effect a new ``+esi_include_onerror`` feature flag needs to be
raised.

Changes to VCL
==============

It is now possible to assign a ``BODY`` variable with either a ``STRING`` type
or a ``BLOB``.

VCL variables
~~~~~~~~~~~~~

New VCL variables to track the beginning of HTTP messages:

- ``req.time``
- ``req_top.time``
- ``resp.time``
- ``bereq.time``
- ``beresp.time``
- ``obj.time``

New ``req.transport`` which returns "HTTP/1" or "HTTP/2" as appropriate.

Other changes to VCL
~~~~~~~~~~~~~~~~~~~~

Where a regular expression literal is expected, it is now possible to have a
concatenation of constant strings. It can be useful when part of the
expression comes from an environment-specific include, or to break a long
expression into multiple lines.

Similarly to ``varnishd`` parameters, it is now possible to have deprecated
aliases of VCL variables. Although there are none so far, aliases will allow
some symbols to be renamed without immediately breaking existing VCL code.

Deprecated VCL aliases have no runtime overhead, they are reified at VCL
compile time.

VMODs
=====

New :ref:`std.strftime()` function for UTC formatting.

It is now possible to declare deprecated aliases of VMOD functions and object
methods, just like VCL aliases. The ``cookie.format_rfc1123()`` was renamed to
:ref:`cookie.format_date()`, and the former was retained as a deprecated alias
of the latter for compatibility.

Deprecated VMOD aliases have no runtime overhead, they are reified at VCL
compile time.

varnishlog
==========

It is now possible to write to the standard output with ``-w -``, to be on par
with the ability to read from the standard input with ``-r -``. This is not
possible in daemon mode.

In a pipe scenario, the backend transaction emits a Start timestamp and both
client and backend transactions emit the Process timestamp.

varnishncsa
===========

It is now possible to write to the standard output with ``-w -``, to be on par
with the ability to read from the standard input with ``-r -``. This is not
possible in daemon mode.

varnishadm
==========

When ``vcl.show`` is invoked without a parameter, it defaults to the active
VCL.

The ``param.set`` command accepts a ``-j`` option. In this case the JSON
output is the same as ``param.show -j`` of the updated parameter.

A new ``debug.shutdown.delay`` command is available in the Varnish CLI for
testing purposes. It can be useful for testing purposes to see how its
environment (service manager, container orchestrator, etc) reacts to a
``varnishd``'s child process taking significant time to ``stop``.

varnishtest
===========

The ``SO_RCVTIMEO_WORKS`` feature check is gone.

The reporting of ``logexpect`` events was rearranged for readability.

XXX: mention the logexpect abort trigger? (it's not documented)

The ``vtc.barrier_sync()`` VMOD function can be used in ``vcl_init`` from now
on.

Changes for developers and VMOD authors
=======================================

The ``SO_RCVTIMEO`` and ``SO_SNDTIMEO`` socket options are now required at
build time since their absence would otherwise prevent some timeouts to take
effect. We no longer check whether they effectively work, hence the removal of
the ``SO_RCVTIMEO_WORKS`` feature check in ``varnishtest``.

Varnish will use libunwind by default when available at configure time, the
``--without-unwind`` configure flag can prevent this and fall back to
libexecinfo to generate backtraces.

There is a new debug storage backend for testing purposes. So far, it can only
be used to ensure that allocation attempts return less space than requested.

There are new C macros for ``VCL_STRANDS`` creation: ``TOSTRAND()`` and
``TOSTRANDS()`` are available in ``vrt.h``.

New utility macros ``vmin[_t]``, ``vmax[_t]`` and ``vlimit[_t]`` available in
``vdef.h``.

The fetch and delivery filters should now be registered and unregistered with
``VRT_AddFilter()`` and ``VRT_RemoveFilter()``.

Dynamic backends are now reference-counted, and VMOD authors must explicitly
track assignments with ``VRT_Assign_Backend()``.

The ``vtc.workspace_reserve()`` VMOD function will zero memory from now on.

When the ``+workspace`` debug flag is raised, workspace logs are no longer
emitted as raw logs disconnected from the task. Having workspace logs grouped
with the rest of the task should help workspace footprint analysis.

It is possible to generate of arbitrary log lines with ``vtc.vsl_replay()``,
which can help testing log processing utilities.

It is also possible to tweak the VXID cache chunk size per thread pool with
the ``debug.xid`` command for the Varnish CLI, which can also help testing
log processing utilities.

``http_IsHdr()`` is now exposed as part of the strict ABI for VMODs.

*eof*
