.. _whatsnew_upgrading_5.1:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 5.1
%%%%%%%%%%%%%%%%%%%%%%%%

varnishd command-line options
=============================

* Added the ``workuser`` parameter to the ``-j`` option.

varnishd parameters
===================

* The size of the shared memory log is now limited to 4G-1b
  (4294967295 bytes).  This places upper bounds on the ``-l``
  command-line option and on the ``vsl_space`` and ``vsm_space``
  parameters.

* Added ``clock_step`` and ``thread_pool_reserve`` (see
  :ref:`ref_param_clock_step`, :ref:`ref_param_thread_pool_reserve`).

* ``thread_pool_stack`` is no longer considered experimental, and is
  more extensively documented, see :ref:`ref_param_thread_pool_stack`.

* ``thread_queue_limit`` only applies to queued client requests, see
  :ref:`ref_param_thread_queue_limit`.

* All parameters are defined on every platform, including those that
  that are not functional on every platform. Most of these involve
  features of the TCP stack, such as ``tcp_keepalive_intvl``,
  ``tcp_keepalive_probes``, ``accept_filter`` and ``tcp_fastopen``.
  The unavailability of a parameter is documented in the output of the
  ``param.show`` command. Setting such a parameter is not an error,
  but has no effect.


Changes to VCL
==============

Type conversions
~~~~~~~~~~~~~~~~

* When ``bereq.backend`` and ``beresp.backend`` are set to a director,
  then they return an actual backend on subsequent reads if the
  director resolves to a backend immediately, or the director otherwise.
  When used in string context, they return the name of the director
  or of the resolved backend.

* DURATION types may be used in boolean contexts, and are evaluated as
  false when the duration is less than or equal to zero, true
  otherwise.

Response codes
~~~~~~~~~~~~~~

Response codes 1000 or greater may now be set in VCL internally.
``resp.status`` is delivered modulo 1000 in client responses.

IP address comparison
~~~~~~~~~~~~~~~~~~~~~

IP addresses can now be compared for equality::

  if (client.ip == remote.ip) {
    call do_if_equal;
  }

The objects are equal if they designate equal socket addresses, not
including the port number. IPv6 addresses are always unequal to IPv4
addresses (the comparison cannot consider v4-mapped IPv6 addresses).

The STEVEDORE type and storage objects
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The data type STEVEDORE for storage backends is now available in VCL
and for VMODs. Storage objects with names of the form
``storage.SNAME`` will exist in a VCL instance, where ``SNAME`` is the
name of a storage backend provided with the ``varnishd`` command-line
option ``-s``. If no ``-s`` option is given, then ``storage.s0``
denotes the default storage.  The object ``storage.Transient`` always
exists, designating transient storage. See :ref:`guide-storage`, and
the notes about ``beresp.storage`` and ``req.storage`` below.

All VCL subroutines (except ``vcl_fini``)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* Added ``return(fail)`` to immediately terminate VCL processing. In
  all cases but ``vcl_synth``, control is directed to ``vcl_synth``
  with ``resp.status`` and ``resp.reason`` set to 503 and "VCL
  failed", respectively. ``vcl_synth`` is aborted on ``return(fail)``.
  ``vcl_fini`` is executed whan a VCL instance in unloaded (enters the
  COLD state) and has no failure condition.

Client-side VCL subroutines
~~~~~~~~~~~~~~~~~~~~~~~~~~~

* ``req.ttl`` is deprecated, see :ref:`vcl(7)`.

vcl_recv
~~~~~~~~

* Added ``req.storage``, which tells Varnish which storage backend to
  use if you choose to save the request body (see
  :ref:`func_cache_req_body`).

* ``return(vcl(LABEL))`` may not be called after a restart. It can
  only be called from the active VCL instance.

vcl_backend_response
~~~~~~~~~~~~~~~~~~~~

* Added ``return(pass(DURATION))`` to set an object to hit-for-pass,
  see :ref:`whatsnew_changes_5.1_hitpass`.

* The object ``beresp.storage`` of type STEVEDORE should now be used
  to set a storage backend; ``beresp.storage_hint`` is deprecated and
  will be removed in a future release. Setting ``beresp.storage_hint``
  to a valid storage will set ``beresp.storage`` as well. If the
  storage is invalid, ``beresp.storage`` is left untouched.

When multiple storage backends have been defined with the ``-s``
command-line option for varnishd, but none is explicitly set in
``vcl_backend_response``, storage selection and the use of the nuke
limit has been reworked (see :ref:`ref_param_nuke_limit`). Previously,
a storage backend was tried first with a nuke limit of 0, and retried
on failure with the limit configured as the ``-p`` parameter
``nuke_limit``. When no storage was specfied, Varnish went through
every one in round-robin order with a nuke limit of 0 before retrying.

Now ``beresp.storage`` is initialized with a storage backend before
``vcl_backend_response`` executes, and the storage set in
``beresp.storage`` after its execution will be used. The configured
nuke limit is used in all cases.

VMOD std
~~~~~~~~

* Added ``std.getenv()``, see :ref:`func_getenv`.

Other changes
=============

* ``varnishstat(1)``:

  * Added the ``cache_hitmiss`` stat to  to count hits on
    hit-for-miss objects.

  * The ``cache_hitpass`` stat now only counts hits on hit-for-pass
    objects.

* ``varnishlog(1)``:

  * Hits on hit-for-miss and hit-for-pass objects are logged with
    the ``HitMiss`` and ``HitPass`` tags, respectively. In each case,
    the log payload is the VXID of the previous transaction in which
    the object was saved in the cache (as with ``Hit``).

  * An entry with the ``TTL`` tag and the prefix ``HFP`` is logged to
    record the duration set for hit-for-pass objects.

* ``varnishncsa(1)``:

  * Clarified the meaning of the ``%r`` formatter, see NOTES in
    :ref:`varnishncsa(1)`.

* ``varnishtest(1)``:

  * Added the ``process``, ``setenv`` and ``write_body`` commands, see
    :ref:`vtc(7)` .

  * ``-reason`` replaces ``-msg`` to set the reason string for a
    response (default "OK").

  * Added ``-cliexpect`` to match expected CLI responses to regular
    expressions.

  * ``varnishtest`` can be stopped with the ``TERM``, ``INT`` of ``KILL``
    signals, but not with ``HUP``. These signals kill the process group,
    so that processes started by running tests are stopped.

* Added the ``vtest.sh`` tool to automate test builds, see
  :ref:`whatsnew_changes_5.1_vtest`.
