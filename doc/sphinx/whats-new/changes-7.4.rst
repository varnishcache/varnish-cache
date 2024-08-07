.. _whatsnew_changes_7.4:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Changes in Varnish **7.4**
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

For information about updating your current Varnish deployment to the
new version, see :ref:`whatsnew_upgrading_7.4`.

A more detailed and technical account of changes in Varnish, with
links to issues that have been fixed and pull requests that have been
merged, may be found in the `change log`_.

.. _change log: https://github.com/varnishcache/varnish-cache/blob/master/doc/changes.rst

varnishd
========

HTTP/2 header field validation is now more strict with respect to
allowed characters.

The :ref:`vcl-step(7)` manual page has been added to document the VCL
state machines.

VCL Tracing
~~~~~~~~~~~

VCL tracing now needs to be explicitly activated by setting the
``req.trace`` or ``bereq.trace`` VCL variables, which are initialized
from the ``feature +trace`` flag. Only if the trace variables are set
will ``VCL_trace`` log records be generated.

Consequently, ``VCL_trace`` has been removed from the default
``vsl_mask``, so any trace records will be emitted by
default. ``vsl_mask`` can still be used to filter ``VCL_trace``
records.

To trace ``vcl_init {}`` and ``vcl_fini {}``, set the ``feature
+trace`` flag while the vcl is loaded/discarded.

Parameters
~~~~~~~~~~

The ``startup_timeout`` parameter now specifically replaces
``cli_timeout`` for the initial startup only.

Changes to VCL
==============

The ``Content-Length`` and ``Transfer-Encoding`` headers are now
protected. For the common use case of ``unset
(be)req.http.Content-Length`` to dismiss a body, ``unset
(be)req.body`` should be used.

varnishlog
==========

Object creation failures by the selected storage engine are now logged
under the ``Error`` tag as ``Failed to create object from %s
%s``.

varnishadm
==========

Tabulation of the ``vcl.list`` CLI output has been modified slightly.

varnishstat
===========

The counter ``MAIN.http1_iovs_flush`` has been added to track the
number of premature ``writev()`` calls due to an insufficient number
of IO vectors. This number is configured through the ``http1_iovs``
parameter for client connections and implicitly defined by the amount
of free workspace for backend connections.

varnishtest
===========

The basename of the test directory is now available as the ``vtcid``
macro to serve as a unique string across concurrently running tests.

The ``varnishd_args_prepend`` and ``varnishd_args_append`` macros have
been added to allow addition of arguments to ``varnishd`` invocations
before and after those added by ``varnishtest`` by default.

``User-Agent`` request and ``Server`` response headers are now created
by default, containing the respective client and server name. The
``txreq -nouseragent`` and ``txresp -noserver`` options disable
addition of these headers.

Changes for developers and VMOD authors
=======================================

Call sites of VMOD functions and methods can now be restricted to
built-in subroutines using the ``$Restrict`` stanza in the VCC file.

``.vcc`` files of VMODs are now installed to
``/usr/share/varnish/vcc`` (or equivalent) to enable re-use by other
tools like code editors.

API Changes
~~~~~~~~~~~

The ``varnishapi`` version has been increased to 3.1 and the
``VSHA256_*``, ``VENC_Encode_Base64()`` and ``VENC_Decode_Base64()``
functions are now exposed.

In ``struct vsmwseg`` and ``struct vsm_fantom``, the ``class`` member
has been renamed to ``category``.

The ``VSB_quote_pfx()`` (and, consequently, ``VSB_quote()``) function
no longer produces ``\v`` for a vertical tab. This improves
compatibility with JSON.

Additions to varnish C header files
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``PTOK()`` macro has been added to ``vas.h`` to simplify error
checking of ``pthread_*`` POSIX functions.

The ``v_cold`` macro has been added to add ``__attribute__((cold))``
on compilers supporting it. It is used for ``VRT_fail()`` to mark
failure code paths as cold.

The utility macros ``ALLOC_OBJ_EXTRA()`` and ``ALLOC_FLEX_OBJ()`` have
been added to ``miniobj.h`` to simplify allocation of objects larger
than a struct and such with a flexible array.

*eof*
