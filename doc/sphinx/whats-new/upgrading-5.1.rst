.. _whatsnew_upgrading_5.1:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 5.1
%%%%%%%%%%%%%%%%%%%%%%%%

Changes to VCL
==============

vcl_backend_response
~~~~~~~~~~~~~~~~~~~~

* Added ``return(pass(DURATION))`` to set an object to hit-for-pass,
  see :ref:`whatsnew_changes_5.1_hitpass`.

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
