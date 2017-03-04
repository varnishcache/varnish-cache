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
