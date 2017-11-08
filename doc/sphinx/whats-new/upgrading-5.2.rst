.. _whatsnew_upgrading_5.2:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 5.2
%%%%%%%%%%%%%%%%%%%%%%%%

Varnish statistics and logging
==============================

There are extensive changes under the hood with respect to statistics
counters, but these should all be transparent at the user-level.

varnishd parameters
===================

The :ref:`ref_param_vsm_space` and ``cli_buffer``
parameters are now deprecated and ignored.  They will be removed
in a future major release.

The updated shared memory implementation manages space automatically, so
it no longer needs :ref:`ref_param_vsm_space`. Memory for the CLI
command buffer is now dynamically allocated.

We have updated the documentation for :ref:`ref_param_send_timeout`,
:ref:`ref_param_idle_send_timeout`, :ref:`ref_param_timeout_idle` and
:ref:`ref_param_ban_cutoff`.

Added the debug bit ``vmod_so_keep``, see :ref:`ref_param_debug` and
the notes about changes for developers below.

Changes to VCL
==============

We have added a few new variables and clarified some matters. VCL
written for Varnish 5.1 should run without changes on 5.2.

Consistent symbol names
~~~~~~~~~~~~~~~~~~~~~~~

VCL symbols originate from various parts of Varnish: there are built-in
variables, subroutines, functions, and the free-form headers. Symbols
may live in a namespace denoted by the ``'.'`` (dot) character as in
``req.http.Cache-Control``. When you create a VCL label, a new symbol
becomes available, named after the label. Storage backends always have
a name, even if you don't specify one, and they can also be accessed in
VCL: for example ``storage.Transient``.

Because headers and VCL names could contain dashes, while subroutines or
VMOD objects couldn't, this created an inconsistency. All symbols follow
the same rules now and must follow the same (case-insensitive) pattern:
``[a-z][a-z0-9_-]*``.

You can now write code like::

  sub my-sub {
      new my-obj = my_vmod.my_constuctor(storage.my-store);
  }

  sub vcl_init {
      call my-sub;
  }

As you may notice in the example above, it is not possible yet to have
dashes in a vmod symbol.

Long storage backend names used to be truncated due to a limitation in
the VSC subsystem, this is no longer the case.

VCL variables
~~~~~~~~~~~~~

``req.hash`` and ``bereq.hash``
-------------------------------

Added ``req.hash`` and ``bereq.hash``, which contain the hash value
computed by Varnish for cache lookup in the current transaction, to
be used in client or backend context, respectively. Their data type
is BLOB, and they contain the raw binary hash.

You can use :ref:`vmod_blob(3)` to work with the hashes::

  import blob;

  sub vcl_backend_fetch {
      # Send the transaction hash to the backend as a hex string
      set bereq.http.Hash = blob.encode(HEX, blob=bereq.hash);
  }

  sub vcl_deliver {
      # Send the hash in a response header as a base64 string
      set resp.http.Hash = blob.encode(BASE64, blob=req.hash);
  }

``server.identity``
-------------------

If the ``-i`` option is not set in the invocation of ``varnishd``,
then ``server.identity`` is set to the host name (as returned by
``gethostname(3)``). Previously, ``server.identity`` defaulted to the
value of the ``-n`` option (or the default instance name if ``-n`` was
not set). See :ref:`varnishd(1)`.

``bereq.is_bgfetch``
--------------------

Added ``bereq.is_bgfetch``, which is readable in backend contexts, and
is true if the fetch takes place in the background. That is, it is
true if Varnish found a response in the cache whose TTL was expired,
but was still in grace time. Varnish returns the stale cached response
to the client, and initiates the background fetch to refresh the cache
object.

``req.backend_hint``
--------------------

We have clarified what happens to ``req.backend_hint`` on a client
restart -- it gets reset to the default backend. So you might want to
make sure that the backend hint gets set the way you want in that
situation.

vmod_std
~~~~~~~~

Added :ref:`func_file_exists`.

New VMODs in the standard distribution
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

See :ref:`vmod_blob(3)`, :ref:`vmod_purge(3)` and
:ref:`vmod_vtc(3)`. Read about them in :ref:`whatsnew_new_vmods`.

Bans
~~~~

We have clarified the interpretation of a ban when a comparison in the
ban expression is attempted against an unset field, see
:ref:`vcl(7)_ban` in :ref:`vcl(7)`.

Other changes
=============

* ``varnishd(1)``:

  * The total size of the shared memory space for logs and counters
    no longer needs to be configured explicitly and therefore the
    second subargument to ``-l`` is now ignored.

  * The default value of ``server.identity`` when the ``-i`` option is
    not set has been changed as noted above.

  * Also, ``-i`` no longer determines the ``ident`` field used by
    ``syslog(3)``; now Varnish is always identified by the string
    ``varnishd`` in the syslog.

  * On a system that supports ``setproctitle(3)``, the Varnish
    management process will appear in the output of ``ps(1)`` as
    ``Varnish-Mgt``, and the child process as ``Varnish-Child``. If
    the ``-i`` option has been set, then these strings in the ps
    output are followed by ``-i`` and the identity string set by the
    option.

  * The ``-f`` option for a VCL source file now honors the
    ``vcl_path`` parameter if a relative file name is used, see
    :ref:`varnishd(1)` and :ref:`ref_param_vcl_path`.

  * The ``-a`` option can now take a name, for example ``-a
    admin=127.0.0.1:88`` to identify an address used for
    administrative requests but not regular client traffic. Otherwise,
    a default name is selected for the listen address (``a0``, ``a1``
    and so forth). Endpoint names appear in the log output, as noted
    below, and may become accessible in VCL in the future.

* ``varnishstat(1)``:

  * In curses mode, the top two lines showing uptimes for the
    management and child processes show the text ``Not Running`` if
    one or both of the processes are down.

  * The interpretation of multiple ``-f`` options in the command line
    has changed slightly, see :ref:`varnishstat(1)`.

  * The ``type`` and ``ident`` fields have been removed from the XML
    and JSON output formats, see :ref:`varnishstat(1)`.

  * The ``MAIN.s_req`` statistic has been removed, as it was identical
    to ``MAIN.client_req``.

  * Added the counter ``req_dropped``. Similar to ``sess_dropped``,
    this is the number of times an HTTP/2 stream was refused because
    the internal queue is full. See :ref:`varnish-counters(7)` and
    :ref:`ref_param_thread_queue_limit`.

* ``varnishlog(1)``:

  * The ``Hit``, ``HitMiss`` and ``HitPass`` log records grew an
    additional field with the remaining TTL of the object at the time
    of the lookup.  While this should greatly help troubleshooting,
    it might break tools relying on those records to get the VXID of
    the object hit during lookup.

    Instead of using ``Hit``, such tools should now use ``Hit[1]``,
    and the same applies to ``HitMiss`` and ``HitPass``.

    The ``Hit`` record also grew two more fields for the grace and
    keep periods.  This should again be useful for troubleshooting.

    See :ref:`vsl(7)`.

  * The ``SessOpen`` log record displays the name of the listen address
    instead of the endpoint in its 3rd field.

    See :ref:`vsl(7)`.

  * The output format of ``VCL_trace`` log records, which appear if
    you have switched on the ``VCL_trace`` flag in the VSL mask, has
    changed to include the VCL configuration name. See :ref:`vsl(7)`
    and :ref:`ref_param_vsl_mask`.

* ``varnishtest(1)`` and ``vtc(7)``:

  * When varnishtest is invoked with ``-L`` or ``-l``, Varnish
    instances started by a test do not clean up their copies of VMOD
    shared objects when they stop. See the note about ``vmod_so_keep``
    below.

  * Added the feature switch ``ignore_unknown_macro`` for test cases,
    see :ref:`vtc(7)`.

* ``varnishncsa(1)``

  * Field specifiers (such as the 1 in ``Hit[1]``) are now limited to
    to 255, see :ref:`varnishncsa(1)`.

* The ``-N`` command-line option, which was previously available for
  ``varnishlog(1)``, ``varnishstat(1)``, ``varnishncsa(1)`` and
  ``varnishhist(1)``, is not compatible with the changed internal
  logging API, and has been retired.

* Changes for developers:

  * The VSM and VSC APIs for shared memory and statistics have
    changed, and may necessitate changes in client applications, see
    :ref:`whatsnew_vsm_vsc_5.2`.

  * Added the ``$ABI`` directive for VMOD vcc declarations, see
    :ref:`whatsnew_abi`.

  * There have been some minor changes in the VRT API, which may be
    used for VMODs and client apps, see :ref:`whatsnew_vrt_5.2`.

  * The VUT API (for Varnish UTilities), which facilitates the
    development of client apps, is now publicly available, see
    :ref:`whatsnew_vut_5.2`.

  * The debug bit ``vmod_so_keep`` instructs Varnish not to clean
    up its copies of VMOD shared objects when it stops. This makes
    it possible for VMOD authors to load their code into a debugger
    after a varnishd crash. See :ref:`ref_param_debug`.

*eof*
