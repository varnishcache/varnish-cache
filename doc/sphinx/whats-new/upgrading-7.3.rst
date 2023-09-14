.. _whatsnew_upgrading_7.3:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 7.3
%%%%%%%%%%%%%%%%%%%%%%%%

New VSL format
==============

The binary format of Varnish logs changed to increase the space for VXIDs from
32 bits to 64. This is not a change that older versions of the Varnish logging
utilities can understand, and the new utilities can also not process old logs.

There is no conversion tool from the old format to the new one, so this should
become a problem only when raw logs are stored for future processing. If old
binary logs need to remain usable, the only solution is to use a compatible
Varnish version and at the time of this release, the 6.0 branch is the only
one without an EOL date.

For developers and VMOD authors: C interface changes requiring adjustments
==========================================================================

Via backends
------------

The new backend argument to the ``VRT_new_backend*()`` functions is optional
and ``NULL`` can be passed to match the previous behavior.

suckaddr
--------

The following functions return or accept ``const`` pointers from now on:

- ``VSA_Clone()``
- ``VSA_getsockname()``
- ``VSA_getpeername()``
- ``VSA_Malloc()``
- ``VSA_Build*()``
- ``VSS_ResolveOne()``
- ``VSS_ResolveFirst()``

``VSA_free()`` has been added to free heap memory allocated by
``VSA_Malloc()`` or one of the ``VSA_Build*()`` functions with a
``NULL`` first argument.

directors
---------

Directors which take and hold references to other directors via
``VRT_Assign_Backend()`` (typically any director which has other
directors as backends) are now expected to implement the new
``.release`` callback of type ``void
vdi_release_f(VCL_BACKEND)``. This function is called by
``VRT_DelDirector()``. The implementation is expected drop any backend
references which the director holds (again using
``VRT_Assign_Backend()`` with ``NULL`` as the second argument).

Failure to implement this callback can result in deadlocks, in
particular during VCL discard.

*eof*
