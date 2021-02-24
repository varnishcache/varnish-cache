..
	Copyright (c) 2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of licens

.. _whatsnew_changes_2019_03:

%%%%%%%%%%%%%%%%%%%%%%
Changes in Varnish 6.2
%%%%%%%%%%%%%%%%%%%%%%

For information about updating your current Varnish deployment to the
new version, see :ref:`whatsnew_upgrading_2019_03`.

A more detailed and technical account of changes in Varnish, with
links to issues that have been fixed and pull requests that have been
merged, may be found in the `change log`_.

.. _change log: https://github.com/varnishcache/varnish-cache/blob/master/doc/changes.rst

varnishd
========

Cache lookups have undergone a number of optimizations, among them
reduction in lock contention, and to shorten and simplify the critical
section of lookup code. We expect that this will improve performance
and scalability.

We have added a "watchdog" for thread pools that will panic the worker
process, causing it to restart, if scheduling tasks onto worker
threads appears to be deadlocking. The length of time until the panic
is set by the :ref:`ref_param_thread_pool_watchdog` parameter. If this
happens, it probably means that thread pools are too small, and you
should consider increasing the parameters
:ref:`ref_param_thread_pool_min`, :ref:`ref_param_thread_pool_max`
and/or :ref:`ref_param_thread_pools`.

.. _whatsnew_changes_params_2019_03:

Parameters
~~~~~~~~~~

The default value of :ref:`ref_param_thread_pool_stack` has been
increased from 48k to 56k on 64-bit platforms and to 52k on 32-bit
platforms.

Recently we had occasional reports of stack overflow, apparently
related to changes in external libraries that are not under control of
the Varnish project (such as glibc). This may also have been related
to stack overflow issues on some platforms when recent versions of
`jemalloc`_, the recommended memory allocator for Varnish, have been
used together with `pcre`_ with JIT compilation enabled. Compiler
hardening flags may also increase stack usage and on some systems such
stack protector flags may be enabled by default. With the addition of
new mitigations to new compiler releases, stack consumption may also
increase on that front.

Tests have shown that Varnish runs stably with the new default stack
size on a number of platforms, under conditions that previously may
have led to stack overflow -- such as ESI includes up to the default
limit of :ref:`ref_param_max_esi_depth`, relatively deep VCL
subroutine call depth, and recent jemalloc together with pcre-jit.

Different sites have different requirements regarding the stack size.
For example, if your site uses a high depth of ESI includes, you are
probably already using an increased value of
:ref:`ref_param_thread_pool_stack`.  If you don't have such
requirements, and you want to reduce memory footprint, you can
consider lowering :ref:`ref_param_thread_pool_stack`, but make sure to
test the result.

.. _jemalloc: http://jemalloc.net/

.. _pcre: https://www.pcre.org/

Some parameters that have been long deprecated are now retired. See
:ref:`whatsnew_upgrading_params_2019_03` in
:ref:`whatsnew_upgrading_2019_03`.

Added :ref:`ref_param_thread_pool_watchdog`, see above.

The :ref:`ref_param_debug` parameter now has a flag ``vcl_keep``. When
the flag is turned on, C sources and shared object libraries that were
generated from VCL sources are retained in the Varnish working
directory (see the notes about ``varnishtest`` below).

For 32-bit platforms, we have increased the default
:ref:`ref_param_workspace_backend` from 16k to 20k accommodate larger
response headers which have become common.

Other changes in varnishd
~~~~~~~~~~~~~~~~~~~~~~~~~

The VCL syntax version is now displayed in a panic message, as 41 for
VCL 4.1 and 40 for VCL 4.0.

Changes to VCL
==============

VCL variables
~~~~~~~~~~~~~

Added ``req.is_hitmiss`` and ``req.is_hitpass``, see :ref:`vcl(7)`.

Other changes to VCL
~~~~~~~~~~~~~~~~~~~~

Runtime restrictions concerning the accessibility of Unix domain
sockets have been relaxed, see :ref:`whatsnew_upgrading_vcl_2019_03`
in :ref:`whatsnew_upgrading_2019_03`.

``return(miss)`` from ``vcl_hit{}`` did never work as intended for the
common case (it actually turned into a pass), so we now removed it and
changed the ``builtin.vcl``. See
:ref:`whatsnew_upgrading_vcl_2019_03`.

VMODs
=====

The type-conversion functions in :ref:`vmod_std(3)` have been reworked
to make them more flexible and easier to use. The ``std.``\ *x2y*
conversion functions are now deprecated. See
:ref:`whatsnew_upgrading_std_conversion_2019_03`.

The function :ref:`directors.lookup()` has been added to
:ref:`vmod_directors(3)`, only for use in ``vcl_init`` or
``vcl_fini``.

varnishlog(1), varnishncsa(1) and vsl(7)
========================================

The performance of bundled log readers, including ``varnishlog`` and
``varnishncsa`` (and any tool using the internal VUT interface for
Varnish utilities) has been improved. They continue reading log
contents in bulk as long as more contents are known to be available,
not stopping as frequently (and unnecessarily) to check the status of
the shared memory mapping.

``varnishlog`` and ``varnishncsa`` now have the ``-R`` command-line
option for rate-limiting, to limit the number of log transactions read
per unit time.  This can make it less likely for log reads to fall
behind and fail with overrun errors under heavy loads. See
:ref:`varnishlog(1)` and :ref:`varnishncsa(1)` for details.

Timing information is now uniformly reported in the log with
microsecond precision.  This affects the tags ``ExpKill`` and
``ExpRearm`` (previously with nanosecond precision).

varnishadm(1) and varnish-cli(7)
================================

The output formats of the ``vcl.list`` and ``backend.list`` commands
have changed, see :ref:`whatsnew_upgrading_backend_list_2019_03` and
:ref:`whatsnew_upgrading_vcl_list_2019_03` in
:ref:`whatsnew_upgrading_2019_03`, as well as :ref:`varnish-cli(7)`
for details.

.. _whatsnew_changes_cli_json:

JSON output
~~~~~~~~~~~

JSON responses, requested with the ``-j`` option, are now possible for
the following commands (see :ref:`varnish-cli(7)`):

* ``status -j``
* ``vcl.list -j``
* ``param.show -j``
* ``ban.list -j``
* ``storage.list -j``
* ``panic.show -j``

The ``-j`` option was already available for ``backend.list``, ``ping``
and ``help`` in previous versions.

For automated parsing of CLI responses (:ref:`varnishadm(1)` output),
we recommend the use of JSON format.

``param.reset <param>``
~~~~~~~~~~~~~~~~~~~~~~~

Added the command ``param.reset`` to reset a parameter's value to its
default, see :ref:`varnish-cli(7)`.

Banning by expiration parameters
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Bans may now be defined with respect to ``obj.ttl``, ``obj.age``,
``obj.grace`` and ``obj.keep``, referring to the expiration and age
properties of the cached object. A ban expression may also be defined
with one of the comparison operators ``<``, ``<=``, ``>`` and ``>=``;
these may only be used with one of the new duration variables for
bans. Duration constants (such as ``5m`` for five minutes of ``3h``
for three hours) may be used in the ``<arg>`` position against which
these objects are compared in a ban expression.

``obj.ttl`` and ``obj.age`` are evaluated with respect to the time at
which the ban was defined, while ``obj.grace`` and ``obj.keep`` are
evaluated as the grace or keep time assigned to the object. So to issue
a ban for objects whose TTL expires more than 5 hours from now and
whose keep parameter is greater than 3 hours, use this expression::

  obj.ttl > 5h && obj.keep > 3h

See :ref:`vcl(7)` and :ref:`users-guide-purging` for details.

varnishstat(1) and varnish-counters(7)
======================================

Added the ``ws_*_overflow`` and ``client_resp_500`` counters to better
diagnose workspace overflow issues, see :ref:`varnish-counters(7)`.

In curses mode, :ref:`varnishstat(1)` now allows use of the ``+`` and
``-`` keys to increase or decrease the refresh rate of the curses
window.

varnishtest
===========

When :ref:`varnishtest(1)` is invoked with either of the ``-L`` or
``-l`` options to retain the temporary directory after tests, the
``vcl_keep`` flag for the :ref:`ref_param_debug` parameter is switched
on (see `Parameters`_ above). This means that C sources and shared
objects generated from VCL can also be inspected after a test. By
default, the temporary directory is deleted after each test.

Since around the time of the last release, we have begun the project
`VTest`_, which is adapted from :ref:`varnishtest(1)`, but is made
available as a stand-alone program useful for testing various HTTP
clients, servers and proxies (not just Varnish). But for the time
being, we still use :ref:`varnishtest(1)` for our own testing.

.. _VTest: https://github.com/vtest/VTest

Changes for developers and VMOD authors
=======================================

Python tools that generate code now require Python 3.

.. _whatsnew_changes_director_api_2019_03:

Directors
~~~~~~~~~

The director API has been changed slightly: The most relevant design
change is that the ``healthy`` callback now is the only means to
determine a director's health state dynamically, the ``sick`` member
of ``struct director`` has been removed. Consequently,
``VRT_SetHealth()`` has been removed and ``VRT_SetChanged()`` added to
update the last health state change time.

Presence of the ``healthy`` callback now also signifies if the
director is considered to have a *probe* with respect to the CLI.

The signature of the ``list`` callback has been changed to reflect the
retirement of the undocumented ``backend.list -v`` parameter and to
add a ``VRT_CTX``.

*eof*
