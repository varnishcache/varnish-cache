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

The :ref:`ref_param_cli_buffer` parameter is deprecated and
ignored. Memory for the CLI command buffer is now dynamically
allocated.

We have updated the documentation for :ref:`ref_param_send_timeout`,
:ref:`ref_param_idle_send_timeout`, :ref:`ref_param_timeout_idle` and
:ref:`ref_param_ban_cutoff`.

Added the debug bit ``vmod_so_keep``, see :ref:`ref_param_debug` and
the notes about changes for developers below.

Changes to VCL
==============

*XXX: intro paragraph*

*XXX: emphasize what most likely needs to be done, if anything,*
*to migrate from 5.1 to 5.2*

*XXX: ... or reassure that you probably don't have to do anything*
*to migrate from 5.1 to 5.2*

XXX: headline changes ...
~~~~~~~~~~~~~~~~~~~~~~~~~

*XXX: the most important changes or additions first*

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
``gethostname(3)``). Previously, ``server.identity`` was set to the
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

XXX: vcl_sub_XXX ...
~~~~~~~~~~~~~~~~~~~~

*XXX: list changes by VCL sub*

XXX: more VCL changes ...
~~~~~~~~~~~~~~~~~~~~~~~~~

*XXX: any more details and new features that VCL authors have to know*

vmod_std
~~~~~~~~

Added :ref:`func_file_exists`.

New VMODs in the standard distribution
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

See :ref:`vmod_blob(3)`, :ref:`vmod_purge(3)` and
:ref:`vmod_vtc(3)`. See :ref:`whatsnew_new_vmods`.

Bans
~~~~

We have clarified the interpretation of a ban when a comparison in the
ban expression is attempted against an unset field, see
:ref:`vcl(7)_ban` in :ref:`vcl(7)`.

Other changes
=============

* ``varnishd(1)``:

  .. XXX phk, a word on -l changes and the implications on how the
         working directory may grow in size? This may be a problem
         when /var/lib/varnish is mounted in RAM.

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
    either or both of the processes are down.

  * The interpretation of multiple ``-f`` options in the command line
    has changed slightly, see :ref:`varnishstat(1)`.

  * The ``type`` and ``ident`` fields have been removed from the XML
    and JSON output formats, see :ref:`varnishstat(1)`.

  * The ``MAIN.s_req`` statistic has been removed, as it was identical
    to ``MAIN.client_req``.

  * *XXX: anything else? stats added, removed or changed?*

* ``varnishlog(1)``:

  * The ``Hit``, ``HitMiss`` and ``HitPass`` log records grew an
    additional field with the remaining TTL of the object at the time
    of the lookup.  While this should greatly help troubleshooting,
    this might break tools relying on those records to get the VXID of
    the object hit during lookup.

    Instead of using ``Hit``, such tools should now use ``Hit[1]``,
    and the same applies to ``HitMiss`` and ``HitPass``.

    The ``Hit`` record also grew two more fields for the grace and
    keep periods.  This should again be useful for troubleshooting.

    See :ref:`vsl(7)`.

  * The ``SessOpen`` log record displays the name of the listen address
    instead of the endpoint in its 3rd field.

    See :ref:`vsl(7)`.

* ``varnishtest(1)`` and ``vtc(7)``:

  * *XXX: changes in test scripting or test code, for example due to VMOD vtc?*

  * When varnishtest is invoked with ``-L`` or ``-l``, Varnish
    instances started by a test do not clean up their copies of VMOD
    shared objects when they stop. See the note about ``vmod_so_keep``
    below.

  * Added the feature switch ``ignore_unknown_macro`` for test cases,
    see :ref:`vtc(7)`.

  * *XXX: ...*

* ``varnishncsa(1)``

  * Field specifiers (such as the 1 in ``Hit[1]``) are now limited to
    to 255, see :ref:`varnishncsa(1)`.

* The ``-N`` command-line option, which was previously available for
  ``varnishlog(1)``, ``varnishstat(1)``, ``varnishncsa(1)`` and
  ``varnishhist(1)``, is not compatible with the changed internal
  logging API, and has been retired.

* *XXX: any other changes in the standard VUT tools*

  * *XXX: ...*

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

  * The project build tools now facilitate the use of sanitizer flags
    (``-fsanitize`` for the compiler and ``ld``), for undefined
    behavior, threads, addresses and memory. See the options
    ``--enable-ubsan``, ``--enable-tsan``, ``--enable-asan`` and
    ``--enable-msan`` for the ``configure`` script generated by
    autoconf.

  * *XXX: ...*

* *XXX: other changes in tools and infrastructure in and around
  Varnish ...*

  * *XXX: anything new about project tools, VTEST & GCOV, etc?*

  * *XXX: ...*
