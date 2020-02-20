**Note: This is a working document for a future release, with running
updates for changes in the development branch. For changes in the
released versions of Varnish, see:** :ref:`whats-new-index`

.. _whatsnew_upgrading_6.3:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 6.3
%%%%%%%%%%%%%%%%%%%%%%%%

**XXX: how to upgrade from previous deployments to this
version. Limited to work that has to be done for an upgrade, new
features are listed in "Changes". Explicitly mention what does *not*
have to be changed, especially in VCL. May include, but is not limited
to:**

TODO: a word on VCL temperature and the ``auto`` change.

For developers and authors of VMODs and API clients
===================================================

The Python 2 EOL is approaching and our build system now favors Python 3. In
the 2020-03-15 release we plan to only support Python 3.

The "vararg" ``VCL_STRING_LIST`` type is superseded by the array-based
``VCL_STRANDS`` type. The deprecated string list will eventually be removed
entirely and VMOD authors are strongly encouraged to convert to strands.
VRT functions working with string list arguments now take strands.

More functions such as ``VRT_Vmod_Init()`` and ``VRT_Vmod_Unload()`` from
the VRT namespace moved away to the Varnish Private Interface (VPI). Such
functions were never intended for VMODs in the first place.

The functions ``VRT_ref_vcl()`` and ``VRT_rel_vcl()`` were renamed to
respectively ``VRT_VCL_Prevent_Discard()`` and ``VRT_VCL_Allow_Discard()``.

Some functions taking ``VCL_IP`` arguments now take a ``VRT_CTX`` in order
to fail in the presence of an invalid IP address.

See ``vrt.h`` for a list of changes since the 6.2.0 release.

We sometimes use Coccinelle_ to automate C code refactoring throughout the
code base. Our collection of semantic patches may be used by VMOD and API
clients authors and are available in the Varnish source tree in the
``tools/coccinelle/`` directory.

.. _Coccinelle: http://coccinelle.lip6.fr/

The ``WS_Reserve()`` function is deprecated and replaced by two functions
``WS_ReserveAll()`` and ``WS_ReserveSize()`` to avoid ambiguous situations.
Its removal is planned for the 2020-09-15 release.

A ``ws_reserve.cocci`` semantic patch can help with the transition.

*eof*
