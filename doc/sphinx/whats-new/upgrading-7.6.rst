.. _whatsnew_upgrading_7.6:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 7.6
%%%%%%%%%%%%%%%%%%%%%%%%

In general, upgrading from Varnish 7.5 to 7.6 should not require any changes
besides the actual upgrade.

The changes mentioned below are considered noteworthy nevertheless:

Noteworthy changes for container workloads
==========================================

When ``varnishd`` runs in a different PID namespace on Linux, typically in a
container deployment, ``VSM_NOPID`` must be added to the environment of other
containers attaching themselves to ``varnishd``'s environment variable. This
will otherwise fail liveness checks performed by VSM consumers.

Noteworthy changes when upgrading varnishd
==========================================

Warning about failed memory locking
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``Warning: mlock() of VSM failed`` message is now emitted when locking of
shared memory segments (via ``mlock(2)``) fails. As Varnish performance may be
severely impacted if shared memory segments are not resident in RAM, users
seeing this message are urged to review the ``RLIMIT_MEMLOCK`` resource control
as set via ``ulimit -l`` or ``LimitMEMLOCK`` with ``systemd(1)``. This is not
new at all, just now the warning has been added to make administrators more
aware.

.. _whatsnew_upgrading_7.6_linux_jail:

Warning if tmpfs is not used
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

On Linux (when the new ``linux`` jail is used), the ``Working directory not
mounted on tmpfs partition`` warning is now emitted if the working directory is
found to reside on a file system other than ``tmpfs``. While other file systems
are supported (and might be the right choice where administrators understand how
to avoid blocking disk IO while ``varnishd`` is writing to shared memory),
``tmpfs`` is the failsafe option to avoid performance issues.

Upgrading VCL
=============

.. _RFC9110: https://www.rfc-editor.org/rfc/rfc9110.html#section-14.4

An issue has been addressed in the ``builtin.vcl`` where backend responses
would fail if they contained a ``Content-Range`` header when no range was
requested. According to `RFC9110`_, this header should just be ignored, yet
some Varnish users might prefer stricter checks. Thus, we decided to change
the ``builtin.vcl`` only and users hitting this issue are advised to call
``vcl_beresp_range`` from custom VCL.

Changes for developers and VMOD authors
=======================================

The VDP filter API has changed. See :ref:`whatsnew_changes_7.6_VDP` for details.

The signature of ``ObjWaitExtend()`` has changed. See
:ref:`whatsnew_changes_7.6_Obj` for details.

``varnishd`` now creates a ``worker_tmpdir`` which can be used by VMODs for
temporary files. See :ref:`ref-vmod-event-functions` for details.

*eof*
