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

*XXX: DRIDI ?  about VMOD purge*

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
to test local configurations, VMODs etc.

See :ref:`vmod_vtc(3)`.

XXX: Any other headline changes ...
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

*XXX: ...*

News for authors of VMODs and Varnish API client applications
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

*XXX: such news may include:*


$ABI [strict|vrt]
-----------------

*XXX: DRIDI ?*

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

The rewrite of the VSM/VSC code similified both APIs and
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
applicaiton must call VSM_Map() and when VSM_StillValid()
indicates that the segment is no longer valid, VSM_Unmap()
should be called to release the segment again.

All in all, this should be simpler and more robust now.

*XXX: changes in VRT*

*EOF*
