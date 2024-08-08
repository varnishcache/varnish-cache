.. _whatsnew_upgrading_7.1:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 7.1
%%%%%%%%%%%%%%%%%%%%%%%%

varnishd
========

Varnish now has an infrastructure in place to rename parameters or VCL
variables while keeping a deprecated alias for compatibility.

Parameters
~~~~~~~~~~

There are plans to rename certain arguments. When this happens, aliases will
not be listed by ``param.show [-j|-l]`` commands, but they will be displayed
by ``param.show [-j] <param>``. Systems operating on top of ``varnishadm`` or
the Varnish CLI can be updated to anticipate this change with the help of the
``deprecated_dummy`` parameter added for testing purposes.

The deprecated ``vsm_space`` parameter was removed. It was ignored and having
no effect since Varnish 6.0.0 and should have disappeared with the 7.0.0
release. The sub-argument of the ``-l`` command line option that was used as
a shorthand for ``vsm_space`` is also no longer accepted.

Command line options
~~~~~~~~~~~~~~~~~~~~

A common pattern when a CLI script is used during startup is to
combine the ``-I`` and ``-f ''`` options to prevent an automatic
startup of the cache process. In this case a start command is usually
present in the CLI script, most likely as the last command. This
enables loading VCLs and potentially VCL labels which require a
specific order if the active VCL is supposed to switch execution to
labels.

To support this pattern, a VCL loaded through the CLI script is no
longer implicitly used if there is no active VCL yet. If no VCL was
loaded through the ``-b`` or ``-f`` options it means that an explicit
``vcl.use`` command is needed before the ``start`` command.

In the scenario described above, that would already be the case since the
desired active VCL would likely need to be loaded last, not eligible for an
implicit ``vcl.use`` since dependencies were loaded first. This change should
not affect existing ``-I`` scripts, but if it does, simply add the missing
``vcl.use`` command.

Other changes in varnishd
~~~~~~~~~~~~~~~~~~~~~~~~~

The ESI parser now recognizes the ``onerror="continue"`` attribute of
the ``<esi:include/>`` XML tag.

The ``+esi_include_onerror`` feature flag controls if the attribute is
honored: If enabled, failure of an include stops ESI processing unless
the ``onerror="continue"`` attribute was set for it.

The feature flag is off by default, preserving the existing behavior
to continue ESI processing despite include failures.

Users of persistent storage engines be advised that objects created
before the introduction of this change cannot carry the
``onerror="continue"`` attribute and, consequently, will be handled as
if it was not present if the ``+esi_include_onerror`` feature flag is
enabled.

Also, as this change is not backwards compatible, downgrades with
persisted storage are not supported across this release.

varnishtest
===========

The deprecated ``err_shell`` command was removed, use ``shell -err`` instead.

Changes for developers and VMOD authors
=======================================

Backends
~~~~~~~~

Backends have reference counters now to avoid the uncertainty of a task
holding onto a dynamic backend for a long time, for example in the waiting
list, with the risk of the backend going away during the transaction.

Assignments should be replaced as such::

    -lvalue = expr;
    +VRT_Assign_Backend(&lvalue, expr);

.. XXX: there should be a coccinelle patch to help.

For backends which are guaranteed at least VCL lifetime, the
respective VMOD can opt out of reference counting with
``VRT_StaticDirector()`` to avoid the reference counting overhead.

Filters
~~~~~~~

Two new functions ``VRT_AddFilter()`` and ``VRT_RemoveFilter()``
manage filters as VDP/VFP pairs. When used as pairs, the filters must
have the same name, otherwise operating with only one fetch or
delivery filter is fine.

Unlike its deprecated predecessors ``VRT_AddVFP()`` and ``VRT_AddVDP()``,
the new ``VRT_AddFilter()`` returns an error string. The ``VRT_RemoveVFP()``
and ``VRT_RemoveVDP()`` functions are also deprecated and kept for now
as wrappers of ``VRT_RemoveFilter()`` without error handling.

VMOD deprecated aliases
~~~~~~~~~~~~~~~~~~~~~~~

A VMOD author can from now on rename a function or object method without
immediately breaking compatibility by declaring the old name as an alias.

In the VMOD descriptor, it is possible to add the following stanza::

    $Alias deprecated_function original_function

    or

    $Alias .deprecated_method object.original_method

This is a good occasion to revisit unfortunate name choices in existing VMODs.

Platform Support
================

systemd
~~~~~~~

To make the selection of the main process deterministic for the kill mode, a
PID file is now expected by default in the varnish service. In a setup where
the service command for ``ExecStart`` is overridden, a ``-P`` option matching
the ``PIDFile`` setting is needed.

*eof*
