.. _whatsnew_changes_CURRENT:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Changes in Varnish **${NEXT_RELEASE}**
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

For information about updating your current Varnish deployment to the
new version, see :ref:`whatsnew_upgrading_CURRENT`.

A more detailed and technical account of changes in Varnish, with
links to issues that have been fixed and pull requests that have been
merged, may be found in the `change log`_.

.. _change log: https://github.com/varnishcache/varnish-cache/blob/master/doc/changes.rst

Security
========

CVE-2023-44487
~~~~~~~~~~~~~~

Also known as the HTTP/2 Rapid Reset Attack, or `VSV 13`_, this vulnerability
is addressed with two mitigations introducing several changes since the 7.4.0
release of Varnish Cache. The first one detects and stops Rapid Reset attacks
and the second one interrupts the processing of HTTP/2 requests that are no
longer open (stream reset, client disconnected etc).

.. _VSV 13: https://varnish-cache.org/security/VSV00013.html

varnishd
========

Parameters
~~~~~~~~~~

The default value of ``cli_limit`` has been increased from 48KB to
64KB to avoid truncating the ``param.show -j`` output for common use
cases.

A new ``pipe_task_deadline`` specifies the maximum duration of a pipe
transaction. The default value is the special value "never" to align with the
former lack of such timeout::

    # both equivalent for now
    param.set pipe_task_deadline never
    param.reset pipe_task_deadline

All the timeout parameters that can be disabled accept the "never" value:

- ``between_bytes_timeout``
- ``cli_timeout``
- ``connect_timeout``
- ``first_byte_timeout``
- ``idle_send_timeout``
- ``pipe_task_deadline``
- ``pipe_timeout``
- ``send_timeout``
- ``startup_timeout``

The :ref:`varnishd(1)` manual advertises the ``timeout`` flag for these
parameters.

The following parameters address the HTTP/2 Rapid Reset attach:

- ``h2_rapid_reset`` (duration below which a reset is considered rapid)
- ``h2_rapid_reset_limit`` (maximum number of rapid resets per period)
- ``h2_rapid_reset_period`` (the sliding period to track rapid resets)

A new bit flag ``vcl_req_reset`` for the ``feature`` parameter interrupts
client request tasks during VCL transitions when an HTTP/2 stream is no longer
open. The result is equivalent to a ``return (fail);`` statement and can save
significant server resources. It can also break setups expecting requests to
always be fully processed, even when they are not delivered.

Bits parameters
~~~~~~~~~~~~~~~

In Varnish 7.1.0 the ``param.set`` command grew a new ``-j`` option that
displays the same output as ``param.show -j`` for the parameter that is
successfully updated.

The goal was to atomically change a value and communicate how a subsequent
``param.show`` would render it. This could be used for consistency checks,
to ensure that a parameter was not changed by a different party. Collecting
how the parameter is displayed can be important for example for floating-point
numbers parameters that could be displayed with different resolutions, or
parameters that can take units and have multiple representations.

Here is a concrete example::

    $ varnishadm param.set -j workspace_client 16384 | jq '.[3].value'
    16384
    $ varnishadm param.set -j workspace_client 128k | jq '.[3].value'
    131072

However, this could not work with bits parameters::

    $ varnishadm param.set -j feature +http2 | jq -r '.[3].value'
    +http2,+validate_headers

If the ``feature`` parameter is changed, reusing the output of ``param.set``
cannot guarantee the restoration that exact value::

    $ varnishadm param.set -j feature +http2,+validate_headers | jq -r '.[3].value'
    +http2,+no_coredump,+validate_headers

To fill this gap, bits parameters are now displayed as absolute values,
relative to none of the bits being set. A list of bits can start with the
special value ``none`` to clear all the bits, followed by specific bits to
raise::

    $ varnishadm param.set -j feature +http2 | jq -r '.[3].value'
    none,+http2,+validate_headers
    $ varnishadm param.set -j feature none,+http2,+validate_headers | jq -r '.[3].value'
    none,+http2,+validate_headers

The output of ``param.show`` and ``param.set`` is now idempotent for bits
parameters, and can be used by a consistency check system to restore a
parameter to its desired value.

Almost all bits parameters are displayed as bits set relative to a ``none``
value. The notable exception is ``vsl_mask`` that is expressed with bits
cleared. For this purpose the ``vsl_mask`` parameter is now displayed as
bits cleared relative to an ``all`` value::


    $ varnishadm param.set -j vsl_mask all,-Debug | jq -r '.[3].value'
    all,-Debug

The special value ``default`` for bits parameters was deprecated in
favor of the generic ``param.reset`` command. It might be removed in a
future release.

Other changes in varnishd
~~~~~~~~~~~~~~~~~~~~~~~~~

The CLI script specified with the ``-I`` option must end with a new line
character or ``varnishd`` will fail to start. Previously, an incomplete last
line would be ignored.

TODO: Should we cover the rapid reset mitigation? It's new since 7.4.0 but not
quite new since "7.4" after the security releases. Should it get a dedicated
prominent headline? Or should it be dispatched in the various sections? Or a
little bit of both?

Changes to VCL
==============

VCL variables
~~~~~~~~~~~~~

A new ``bereq.task_deadline`` variable is available in ``vcl_pipe`` to
override the ``pipe_task_deadline`` parameter.

All the timeouts that can be overridden in VCL can be unset as well:

- ``bereq.between_bytes_timeout``
- ``bereq.connect_timeout``
- ``bereq.first_byte_timeout``
- ``bereq.task_deadline``
- ``sess.idle_send_timeout``
- ``sess.send_timeout``
- ``sess.timeout_idle``
- ``sess.timeout_linger``

They are unset by default, and if they are read unset, the parameter value is
returned. If the timeout parameter was disabled with the "never" value, it is
capped in VCL to the maximum decimal number (999999999999.999). It is not
possible to disable a timeout in VCL.

ESI
~~~

In the 7.3.0 release a new error condition was added to ESI fragments. A
fragment is considered valid only for the response status code 200 and 204.

However, when introduced it also changed the default behavior of the feature
flag ``esi_include_onerror`` in an inconsistent way.

The behavior is reverted to the traditional Varnish handling of ESI, and the
effect of the feature flag is clarified:

- by default, fragments are always included, even errors
- the feature flag ``esi_include_onerror`` enable processing of the
  ``onerror`` attribute of the ``<esi:include>`` tag
- ``onerror="continue"`` allows a parent request to resume its delivery after
  a sub-request failed
- when streaming is disabled for the sub-request, the ESI fragment is omitted
  as mandated by the ESI specification

See :ref:`users-guide-esi` for more information.

Other changes to VCL
~~~~~~~~~~~~~~~~~~~~

The new ``+fold`` flag for ACLs merges adjacent subnets together and optimize
out subnets for which there exist another all-encompassing subnet.

VMODs
=====

A new :ref:`vmod_h2(3)` can override the ``h2_rapid_reset*`` parameters on a
per-session basis.

varnishlog
==========

The ``SessClose`` record may contain the ``RAPID_RESET`` reason. This can be
used to monitor attacks successfully mitigated or detect false positives.

When the ``feature`` flag ``vcl_req_reset`` is raised, an interrupted client
logs a ``Reset`` timestamps, and the response status code 408 is logged.

When a ``BackendClose`` record includes a reason field, it now shows the
reason tag (for example ``RX_TIMEOUT``) instead of its description (Receive
timeout) to align with ``SessClose`` records. See :ref:`vsl(7)`.

The ``ExpKill`` tag can be used to troubleshoot a cache policy. It is masked
by default because it is very verbose and requires a good understanding of
Varnish internals in the expiry vicinity.

A new field with the number of hits is present in the ``EXP_Expired`` entry of
an object. Objects removed before they expired are now logged a new entry
``EXP_Removed``, removing a blind spot. Likewise, purged objects are no longer
logged as expired, but removed instead.  The ``EXP_expire`` entry formerly
undocumented was renamed to ``EXP_Inspect`` for clarity and consistency. A new
``VBF_Superseded`` entry explains which object is evicting another one.

varnishncsa
===========

A new custom format ``%{Varnish:default_format}x`` expands to the output
format when nothing is specified. This allows enhancing the default format
without having to repeat it::

    varnishncsa -F ``%{Varnish:default_format}x %{Varnish:handling}x``

varnishstat
===========

A new ``MAIN.sc_rapid_reset`` counter counts the number of HTTP/2 connections
closed because the number of rapid resets exceed the limit over the configured
period.

Its ``MAIN.req_reset`` counterpart counts the number of time a client task was
prematurely failed because the HTTP/2 stream it was processing was no longer
open and the feature flag ``vcl_req_reset`` was raised.

A new counter ``MAIN.n_superseded`` adds visibility on how many objects are
inserted as the replacement of another object in the cache. This can give
insights regarding the nature of churn in a cache.

varnishtest
===========

When an HTTP/2 stream number does not matter and the stream is handled in a
single block, the automatic ``next`` identifier can be used::

    server s1 {
           stream next {
                   rxreq
                   txresp
           } -run
    } -start

It is now possible to include other VTC fragments::

    include common-server.vtc common-varnish.vtc

An include command takes at least one file name and expands it in place of the
include command itself. There are no guards against recursive includes.

Changes for developers and VMOD authors
=======================================

The ``VSB_tofile()`` function can work with VSBs larger than ``INT_MAX`` and
tolerate partial writes.

The semantics for ``vtim_dur`` changed so that ``INFINITY`` is interpreted as
never timing out. A zero duration that was used in certain scenarios as never
timing out is now interpreted as non-blocking or when that is not possible,
rounded up to one millisecond. A negative value in this context is considered
an expired deadline as if zero was passed, giving a last chance for operations
to succeed before timing out.

To support this use case, new functions convert ``vtim_dur`` to other values:

- ``VTIM_poll_tmo()`` computes a timeout for ``poll(2)``
- ``VTIM_timeval_sock()`` creates a ``struct timeval`` for ``setsockopt(2)``

The value ``NAN`` is used to represent unset timeouts in VCL with one notable
exception. The ``struct vrt_backend`` duration fields cannot be initialized to
``NAN`` and zero was the unset value, falling back to parameters. Zero will
disable a timeout in a backend definition (which can be overridden by VCL
variables) and a negative value will mean unset.

This is an API breakage of ``struct vrt_backend`` and its consumers.

Likewise, VMODs creating their own lock classes with ``Lck_CreateClass()``
must stop using zero an indefinite ``Lck_CondWaitTimeout()``.

*eof*
