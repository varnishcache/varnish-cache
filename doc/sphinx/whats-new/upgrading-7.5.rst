.. _whatsnew_upgrading_7.5:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish **7.5**
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

Logs
====

The optional reason field of ``BackendClose`` records changed from a
description (for example "Receive timeout") to a reason tag (for example
``RX_TIMEOUT``). This will break VSL queries based on the reason field.

Using a tag should however make queries more reliable::

    # before
    varnishlog -q 'BackendClose ~ "Receive timeout"'

    # after
    varnishlog -q 'BackendClose[4] eq RX_TIMEOUT'

Such queries would no longer break when a description is changed.

Timeouts
========

The value zero for timeouts could mean either timing out immediately, never
timing out, or not having a value and falling back to another. The semantic
changed so zero always means non-blocking, timing out either immediately after
checking whether progress is possible, or after a millisecond in parts where
this can't practically be done in a non-blocking fashion.

In order to disable a timeout, that is to say never time out, the value
``INFINITY`` is used (or tested with ``isinf()``).

In the places where a timeout setting may fall back to another value, like
VCL variables falling back to parameters, ``NAN`` is used to convey the lack
of timeout setting.

VCL
~~~

All the timeouts backed by parameters can now be unset, meaning that setting
the value zero no longer falls back to parameters.

Parameters
~~~~~~~~~~

All the timeout parameters that can be disabled are now documented with the
``timeout`` flag, meaning that they can take the special value ``never`` for
that purpose::

    varnishadm param.set pipe_timeout never

The parameters ``idle_send_timeout`` and ``timeout_idle`` are now
limited to a maximum of 1 hour.

VRT
~~~

For VMOD authors, it means that the ``vtim_dur`` type expects ``INFINITY`` to
never time out or ``NAN`` to not set a timeout.

For VMOD authors managing backends, there is one exception where a timeout
fallback did not change from zero to ``NAN`` in ``struct vrt_backend``. The
``vtim_dur`` fields must take a negative value in order to fall back to the
respective parameters due to a limitation in how VCL is compiled.

As a convenience, a new macro ``VRT_BACKEND_INIT()`` behaves like ``INIT_OBJ``
but initializes timeouts to a negative value.

VCL COLD events have been fixed for directors vs. VMODs: VDI COLD now
comes before VMOD COLD.

Other changes relevant for VMOD / VEXT authors
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Transports are now responsible for calling ``VDP_Close()`` in all
cases.

Storage engines are now responsible for deciding which
``fetch_chunksize`` to use. When Varnish-Cache does not know the
expected object size, it calls the ``objgetspace`` stevedore function
with a zero ``sz`` argument.

``(struct req).filter_list`` has been renamed to ``vdp_filter_list``.

*eof*
