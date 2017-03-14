.. _whatsnew_upgrading_5.1:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 5.1
%%%%%%%%%%%%%%%%%%%%%%%%

varnishd command-line options
=============================

If you have to change anything at all for version 5.1, then most
likely the command-line options for `varnishd` in your start scripts,
because we have tightened restrictions on which options may be used
together. This has served mainly to clarify the use of options for
testing purposes, for example using ``varnishd -C`` to check a VCL
source for syntactic correctness. We have also added some new options.

The details are given in :ref:`varnishd(1)`, but here's a summary:

* Added ``-I clifile`` to run CLI commands at startup, before the
  worker process starts. See :ref:`whatsnew_clifile`.

* More than one ``-f`` option is now permitted, to pre-load VCL
  instances at startup. The last of these becomes the "boot" instance
  that is is active at startup.

* Either ``-b`` or one or more ``-f`` options must be specified, but
  not both, and they cannot both be left out, unless ``-d`` is used to
  start `varnishd` in debugging mode. If the empty string is specified
  as the sole ``-f`` option, then `varnishd` starts without starting
  the worker process, and the management process will accept CLI
  commands.

* Added ``-?`` to print the usage message, which is only printed for
  this option.

* Added the ``-x`` option to print certain kinds of documentation and
  exit. When ``-x`` is used, it must be the only option.

* Only one of ``-F`` or ``-d`` may be used, and neither of these can
  be used with ``-C``.

* Added the ``workuser`` parameter to the ``-j`` option.

varnishd parameters
===================

* The size of the shared memory log is now limited to 4G-1b
  (4294967295 bytes).  This places upper bounds on the ``-l``
  command-line option and on the ``vsl_space`` and ``vsm_space``
  parameters.

* Added ``clock_step``, ``thread_pool_reserve`` and ``ban_cutoff`` (see
  :ref:`ref_param_clock_step`, :ref:`ref_param_thread_pool_reserve`,
  :ref:`ref_param_ban_cutoff`).

* ``thread_pool_stack`` is no longer considered experimental, and is
  more extensively documented, see :ref:`ref_param_thread_pool_stack`.

* ``thread_queue_limit`` only applies to queued client requests, see
  :ref:`ref_param_thread_queue_limit`.

* ``vcl_dir`` and ``vmod_dir`` are deprecated and will be removed from
  a future release, use ``vcl_path`` and ``vmod_path`` instead (see
  :ref:`ref_param_vcl_path`, :ref:`ref_param_vmod_path`).

* All parameters are defined on every platform, including those that
  that are not functional on every platform. Most of these involve
  features of the TCP stack, such as ``tcp_keepalive_intvl``,
  ``tcp_keepalive_probes``, ``accept_filter`` and ``tcp_fastopen``.
  The unavailability of a parameter is documented in the output of the
  ``param.show`` command. Setting such a parameter is not an error,
  but has no effect.


Changes to VCL
==============

VCL written for Varnish 5.0 will very likely work without changes in
version 5.1. We have added some new elements and capabilities to the
language (which you might like to start using), clarified some
matters, and deprecated some little-used language elements.

Type conversions
~~~~~~~~~~~~~~~~

We have put some thought to the interpretation of the ``+`` and ``-``
operators for various combinations of operands with differing data
types, many of which count as corner cases (what does it mean, for
example, to subtract a string from an IP address?). Recall that ``+``
denotes addition for numeric operands, and string concatenation for
string operands; operands may be converted to strings and
concatenated, if a string is expected and there is no sensible numeric
interpretation.

The semantics have not changed in nearly all cases, but the error
messages for illegal combinations of operands have improved. Most
importantly, we have taken the time to review these cases, so this
will be the way VCL works going forward.

To summarize:

* If both operands of ``+`` or ``-`` are one of BYTES, DURATION, INT
  or REAL, then the result has the same data type, with the obvious
  numeric interpretation. If such an expression is evaluated in a
  context that expects a STRING (for example for assignment to a
  header), then the arithmetic is done first, and the result it
  converted to a STRING.

* INTs and REALs can be added or subtracted to yield a REAL.

* A DURATION can be added to or subtracted from a TIME to yield a
  TIME.

* No other combinations of operand types are legal with ``-``.

* When a ``+`` expression is evaluated in a STRING context, then for
  all other combinations of operand data types, the operands are
  converted to STRINGs and concatenated.

* If a STRING is not expected for the ``+`` expression, then no other
  combination of data types is legal.

Other notes on data types:

* When ``bereq.backend`` is set to a director, then it returns an
  actual backend on subsequent reads if the director resolves to a
  backend immediately, or the director otherwise. If ``bereq.backend``
  was set to a director, then ``beresp.backend`` references the backend
  to which it was set for the fetch.  When either of these is used in
  string context, it returns the name of the director or of the
  resolved backend.

* Comparisons between symbols of type BACKEND now work properly::

      if (bereq.backend == foo.backend()) {
          # do something specific to the foo backends
      }

* DURATION types may be used in boolean contexts, and are evaluated as
  false when the duration is less than or equal to zero, true
  otherwise.

* INT, DURATION and REAL values can now be negative.

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
  ``vcl_fini`` is executed when a VCL instance in unloaded (enters the
  COLD state) and has no failure condition.

* VCL failure is invoked on any attempt to set one of the fields in the
  the first line of a request or response to the empty string, such
  as ``req.url``, ``req.proto``, ``resp.reason`` and so forth.

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

For the case where multiple storage backends have been defined with
the ``-s`` command-line option for varnishd, but none is explicitly
set in ``vcl_backend_response``, storage selection and the use of the
nuke limit has been reworked (see
:ref:`ref_param_nuke_limit`). Previously, a storage backend was tried
first with a nuke limit of 0, and retried on failure with the limit
configured as the ``-p`` parameter ``nuke_limit``. When no storage was
specified, Varnish went through every one in round-robin order with a
nuke limit of 0 before retrying.

Now ``beresp.storage`` is initialized with a storage backend before
``vcl_backend_response`` executes, and the storage set in
``beresp.storage`` after its execution will be used. The configured
nuke limit is used in all cases.

vmod_std
~~~~~~~~

* Added ``std.getenv()``, see :ref:`func_getenv`.

* Added ``std.late_100_continue()``, see :ref:`func_late_100_continue`.

Other changes
=============

* The storage backend type umem, long in disuse, has been retired.

* ``varnishstat(1)``:

  * Added the ``cache_hitmiss`` stat to count hits on hit-for-miss
    objects.

  * The ``cache_hitpass`` stat now only counts hits on hit-for-pass
    objects.

  * ``fetch_failed`` is incremented for any kind of fetch failure --
    when there is a failure after ``return(deliver)`` from
    ``vcl_backend_response``, or when control is diverted to
    ``vcl_backend_error``.

  * Added the ``n_test_gunzip`` stat, which is incremented when
    Varnish verifies a compressed response from a backend -- this
    operation was previously counted together with ``n_gunzip``.

  * Added the ``bans_lurker_obj_killed_cutoff`` stat to count the
    number of objects killed by the ban lurker to keep the number of
    bans below ``ban_cutoff``.

* ``varnishlog(1)``:

  * Hits on hit-for-miss and hit-for-pass objects are logged with
    the ``HitMiss`` and ``HitPass`` tags, respectively. In each case,
    the log payload is the VXID of the previous transaction in which
    the object was saved in the cache (as with ``Hit``).

  * An entry with the ``TTL`` tag and the prefix ``HFP`` is logged to
    record the duration set for hit-for-pass objects.

  * Added ``vxid`` as a lefthand side token for VSL queries, allowing
    for queries that search for transaction IDs in the log. See
    :ref:`vsl-query(7)`.

* ``varnishncsa(1)``:

  * Clarified the meaning of the ``%r`` formatter, see NOTES in
    :ref:`varnishncsa(1)`.

  * Clarified the meaning of the ``%{X}i`` and ``%{X}o`` formatters
    when the header X appears more than once (the last occurrence is
    is used).

* ``varnishtest(1)``:

  * Added the ``setenv`` and ``write_body`` commands, see :ref:`vtc(7)`.

  * ``-reason`` replaces ``-msg`` to set the reason string for a
    response (default "OK").

  * Added ``-cliexpect`` to match expected CLI responses to regular
    expressions.

  * Added the ``-match`` operator for the ``shell`` command.

  * Added the ``-hdrlen`` operator to generate a header with a
    given name and length.

  * The ``err_shell`` command is deprecated, use ``shell -err
    -expect`` instead.

  * The ``${bad_backend}`` macro can now be used for a backend that
    is always down, which does not require a port definition (as does
    ``${bad_ip}`` in a backend definition).

  * ``varnishtest`` can be stopped with the ``TERM``, ``INT`` of ``KILL``
    signals, but not with ``HUP``. These signals kill the process group,
    so that processes started by running tests are stopped.

* Added the ``vtest.sh`` tool to automate test builds, see
  :ref:`whatsnew_changes_5.1_vtest`.
