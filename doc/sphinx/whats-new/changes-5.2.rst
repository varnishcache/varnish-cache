.. _whatsnew_changes_5.2:

Changes in Varnish 5.2
======================

Varnish 5.2 is mostly changes under the hood so most varnish
installations will be able to upgrade with no modifications.

.. _whatsnew_new_vmods:

New VMODs in the standard distribution
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

We have added three new VMODs to the varnish project.

VMOD blob
---------

We have added the variables ``req.hash`` and ``bereq.hash`` to VCL,
which contain the hash value computed by Varnish for the current
request, for use in cache lookup. Their data type is BLOB, which
represents opaque data of any length -- the new variables contain
the raw binary hashes.

This is the first time that an element of standard VCL has the BLOB
type (BLOBs have only been used in third-party VMODs until now). So we
have added VMOD blob to facilitate their use. In particular, the VMOD
implements binary-to-text encodings, for example so that you can
assign the hash to a header as a base64 or hex string. It also
provides some other utilities such as getting the length of a BLOB or
testing BLOBs for equality.

See :ref:`vmod_blob(3)`.

VMOD purge
----------

Before the introduction of ``vcl 4.0`` there used to be a ``purge`` function
instead of a ``return(purge)`` transition. This module works like old-style
VCL purges (which should be used from both ``vcl_hit`` and ``vcl_miss``) and
provides more capabilities than regular purges, and lets you know how many
objects were affected.

See :ref:`vmod_purge(3)`.

VMOD vtc
--------

As long as we have had VMODs, we had an internal vmod called ``vmod_debug`` 
which were used with ``varnishtest`` to exercise the VMOD related parts of
``varnishd``.  Over time this vmod grew other useful functions for writing
test-cases.

We only distribute ``vmod_debug`` in source releases, because it has some
pretty evil functionality, for instance ``debug.panic()``.

We have split the non-suicidal test-writing stuff from ``vmod_debug``
into a new ``vmod_vtc``, which is included in binary releases from
now on, in order to make it easier for people to use ``varnishtest``
to test local configurations, VMODs etc. The main highlight is that you
can now synchronize barriers with VCL code thanks to ``vmod_vtc``, other
conveniences were added like workspace manipulations.

See :ref:`vmod_vtc(3)`.

XXX: Any other headline changes ...
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

*XXX: ...*

News for authors of VMODs and Varnish API client applications
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

*XXX: such news may include:*

.. _whatsnew_abi:

$ABI [strict|vrt]
-----------------

VMOD authors have the option of only integrating with the blessed
interface provided by ``varnishd`` or go deeper in the stack. As
a general rule of thumb you are considered "on your own" if your
VMOD uses more than the VRT (Varnish RunTime) and it is supposed
to be built for the exact Varnish version.

Varnish was already capable of checking the major/minor VRT version
a VMOD was built against, or require the exact version, but picking
one or the other depended on how Varnish was built.

VMOD authors can now specify whether a module complies to the VRT
and only needs to be rebuilt when breaking changes are introduced
by adding ``$ABI vrt`` to their VCC descriptor. The default value
is ``$ABI strict`` when omitted, and all VMODs from the standard
Varnish distribution have a strict requirement.

.. _whatsnew_vsm_vsc_5.2:

VSM/VSC API changes
-------------------

The export of statistics counters via shared memory has been
overhauled to get rid of limitations which made sense 11 years
ago but not so much now.

A set of statistics counters are now fully defined in a ``.vsc``
file which is processed by the ``vsctool.py`` script into a .c and
.h file, which is compiled into the relevant body of code.

This means that statistics counters are now self-describing in
shared memory, and ``varnishstat`` or other VSC-API using programs
have no compiled in knowledge about which counters exist or how
to treat them.

This paves the way for VMODs or maybe even VCL to define
custom counters, and have them show up in varnishstat and
other VSC-API based programs just like the rest of the counters.

The rewrite of the VSM/VSC code simplified both APIs and
made them much more robust but code calling into these APIs
will have to be updated to match.

The necessary changes mostly center around detecting if the
varnishd management/worker process has restarted.

In the new VSM-api once setup is done, VSM_Attach() latches
on to a running varnishd master process and stays there.

VSM_Status() updates the in-memory list of VSM segments, and
returns information about the master and worker proces:
Are they running?  Have they been restarted?  Have VSM segments
been added/deleted?

Each VSM segment is now a separate piece of shared memory
and the name of the segment can be much longer now.

Before the actual shared memory can be accessed, the
application must call VSM_Map() and when VSM_StillValid()
indicates that the segment is no longer valid, VSM_Unmap()
should be called to release the segment again.

All in all, this should be simpler and more robust now.

.. _whatsnew_vrt_5.2:

VRT API changes
---------------

``VRT_purge`` now fails a transaction instead of panicking when used
outside of ``vcl_hit`` or ``vcl_miss``. It also returns the number
of purged objects.

New ``http_CollectHdrSep`` function, ``http_*`` symbols and associated
data structures are part of the curated Varnish RunTime. Using them
does not require strict ABI compliance.

.. _whatsnew_vut_5.2:

New VUT API
-----------

XXX: document once merged

*EOF*
