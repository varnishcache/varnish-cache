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

* Added the ``cache_hitmiss`` stat to ``varnishstat(1)`` to count hits on
  hit-for-miss objects.

* The ``cache_hitpass`` stat now only counts hits on hit-for-pass objects.

* An entry with the ``TTL`` tag and the prefix ``HFP`` is logged in
  ``varnishlog(1)`` to record the duration set for hit-for-pass objects.
