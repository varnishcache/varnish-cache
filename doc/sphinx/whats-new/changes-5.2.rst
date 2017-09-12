.. _whatsnew_changes_5.2:

Changes in Varnish 5.2
======================

*XXX: preamble*

Varnish statistics
~~~~~~~~~~~~~~~~~~

The export of statistics counters via shared memory has been
overhauled to get rid of limitations which made sense 11 years
ago but not so much now.

Statistics counters are now self-describing in shared memory,
paving the way so that VMODs or maybe even VCL can define
counters in the future, and have them show up in varnishstat
and other VSC-API based programs.

.. _whatsnew_new_vmods:

New VMODs in the standard distribution
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

*XXX: introductory paragraphs about new VMODs*

VMOD blob
---------

We have added the variables ``req.hash`` and ``bereq.hash`` to VCL,
which contain the hash value computed by Varnish for the current
request, for use in cache lookup. Their data type is BLOB, which
represents arbitrary data of any length -- the new variables contain
the raw binary hashes.

This is the first time that an element of standard VCL has the BLOB
type (BLOBs have only been used in third-party VMODs until now). So we
have added VMOD blob to facilitate their use. In particular, the VMOD
implements binary-to-text encodings, for example so that you can
assign the hash to a header as a base64 or hex string. It also
provides some other utilities such as getting the length of a BLOB or
testing BLOBs for equality. See :ref:`vmod_blob(3)`.

VMOD purge
----------

*XXX: about VMOD purge*

VMOD vtc
--------

*XXX: about VMOD vtc*

XXX: Any other headline changes ...
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

*XXX: ...*

News for authors of VMODs and Varnish API client applications
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

*XXX: such news may include:*

VSM/VSC API changes
-------------------

The rewrite of the VSM/VSC code has similified the API and
made it much more robust, and code calling into these APIs
will have to be updated to match.

The necessary changes mostly center around detecting if the
varnishd management/worker process has restarted.

In the new VSM-api once setup is done, VSM_Attach() latches
on to a running varnishd master process and stays there.

VSM_Status() returns information about the master and worker
process, if they are running, if they have been restarted
(since the previous call to VSM_Status()) and updates the
in-memory list of VSM segments.

Each VSM segment is now a separate piece of shared memory
and the name of the segment can be much longer now.

Before the actual shared memory can be accessed, the
applicaiton must call VSM_Map() and when VSM_StillValid()
indicates that the segment is no longer valid, VSM_Unmap()
should be called to release the segment again.

All in all, this should be simpler and more robust now.

* *XXX: changes in VRT*

*EOF*
