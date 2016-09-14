.. _whatsnew_upgrading_5_0:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 5.0
%%%%%%%%%%%%%%%%%%%%%%%%

Changes to VCL
==============

vcl_recv {}
~~~~~~~~~~~

* added ``return(vcl(label))`` to switch to the vcl labeled `label`

vcl_hit {}
~~~~~~~~~~

* replace ``return(fetch)`` with ``return(miss)``

vcl_backend_* {}
~~~~~~~~~~~~~~~~

* added read access to ``remote.ip``, ``client.ip``, ``local.ip`` and
  ``server.ip``

vcl_backend_fetch {}
~~~~~~~~~~~~~~~~~~~~

* added write access to ``bereq.body``, the request body, only
  supported with ``unset`` yet.

* We now send request bodies by default (see :ref:_whatsnew_changes_5.0).
  To keep the previous behaviour, add the following code before any
  ``return()``::

	if (bereq.method == "GET") {
	    unset bereq.body;
	}


vcl_backend_error {}
~~~~~~~~~~~~~~~~~~~~

* added write access to ``beresp.body``, the response body.  This is
  planned to replace ``synthetic()`` in future releases.

vcl_deliver {}
~~~~~~~~~~~~~~

* added read access to ``obj.ttl``, ``obj.age``, ``obj.grace`` and
  ``obj.keep``

vcl_synth {}
~~~~~~~~~~~~

* added write access to ``resp.body``, the response body. This is
  planned to replace ``synthetic()`` in future releases.

Management interface
====================


Changes to parameters
=====================

* added ``ban_lurker_holdoff``

* removed ``session_max``

  this parameter actually had no effect since 4.0 and will likely be
  added back later.
