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

varnishd
========

Parameters
~~~~~~~~~~

**XXX changes in -p parameters**

The default value of ``cli_limit`` has been increased from 48KB to
64KB to avoid truncating the ``param.show -j`` output for common use
cases.

The ``vsl_mask`` parameter accepts a new special value "all" that enables
logging of all VSL tags, the counterpart of "none".

.. I am not sure if "absolute value" is the best name here. It is
   "relative to all", but I do not have a better idea

This allows all bits parameters to be set atomically to an absolute
value, as in::

    param.set vsl_mask all,-Debug,-ExpKill

The ``param.show`` output prints the absolute value. This enables
operations to atomically set a bits parameter, relative or absolute,
and collect the absolute value::

    param.show vsl_mask
    200
    vsl_mask
            Value is: all,-Debug,-ExpKill
    [...]

The ``param.set`` command in JSON mode (``-j argument``) prints the
``param.show`` JSON output after successfully updating a
parameter. The ``param.reset`` command now shares the same behavior.

The special value ``default`` for bits parameters was deprecated in
favor of the generic ``param.reset`` command. It will be removed in a
future release.

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

Other changes in varnishd
~~~~~~~~~~~~~~~~~~~~~~~~~

The CLI script specified with the ``-I`` option must end with a new line
character or ``varnishd`` will fail to start. Previously, an incomplete last
line would be ignored.

TODO: Should we cover the rapid reset mitigation? It's new since 7.4.0 but not
quite new since "7.4" after the security releases. Should it get a dedicated
prominent headline? Or should it be dispatched in the various sections? Or a
little bit of both?

List of rapid reset changes:
  - param h2_rapid_reset
  - param h2_rapid_reset_limit
  - param h2_rapid_reset_period
  - MAIN.sc_rapid_reset counter
  - SessClose tag RAPID_RESET
  - vmod_h2 (with per-h2_sess h2_rapid_* parameters)

List of reset changes:
  - param feature +vcl_req_reset
  - MAIN.req_reset counter
  - VSL Timestamp:Reset
  - status 408 logged for reset streams

Changes to VCL
==============

VCL variables
~~~~~~~~~~~~~

**XXX new, deprecated or removed variables, or changed semantics**

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

**XXX changes in the bundled VMODs**

varnishlog
==========

**XXX changes concerning varnishlog(1) and/or vsl(7)**

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

varnishadm
==========

**XXX changes concerning varnishadm(1) and/or varnish-cli(7)**

varnishstat
===========

**XXX changes concerning varnishstat(1) and/or varnish-counters(7)**

A new counter ``MAIN.n_superseded`` adds visibility on how many objects are
inserted as the replacement of another object in the cache. This can give
insights regarding the nature of churn in a cache.

varnishtest
===========

**XXX changes concerning varnishtest(1) and/or vtc(7)**

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

**XXX changes concerning VRT, the public APIs, source code organization,
builds etc.**

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
