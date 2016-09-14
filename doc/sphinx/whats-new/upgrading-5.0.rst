.. _whatsnew_upgrading_5_0:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 5.0
%%%%%%%%%%%%%%%%%%%%%%%%

Changes to VCL
==============

* All VCL Objects should now be defined before used
  * in particular, this is now required for ACLs. The error message
    for ACLs being used before being defined is confusing - see PR #2021
    ``Name <acl> is a reserved name``

backend ... {}
~~~~~~~~~~~~~~

* added ``.proxy_header`` attribute with possible values of 1 and 2
  for PROXY Protocol Version 1 and 2

vcl_recv {}
~~~~~~~~~~~

* added ``return(vcl(label))`` to switch to the vcl labeled `label`
* ``rollback`` is now ``std.rollback(req)``

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

* added write access to ``beresp.body``, the response body.  This may
  replace ``synthetic()`` in future releases.

vcl_deliver {}
~~~~~~~~~~~~~~

* added read access to ``obj.ttl``, ``obj.age``, ``obj.grace`` and
  ``obj.keep``

vcl_synth {}
~~~~~~~~~~~~

* added write access to ``resp.body``, the response body. This may
  replace ``synthetic()`` in future releases.

Management interface
====================

* to disable CLI authentication, use ``-S none``

* ``n_waitinglist`` statistic removed

Changes to parameters
=====================

* added ``ban_lurker_holdoff``

* removed ``session_max``

  this parameter actually had no effect since 4.0 and will likely be
  added back later.

* ``vcl_path`` is now a colon-separated list of directories, replacing
  ``vcl_dir``

* ``vmod_path`` is now a colon-separated list of directories, replacing
  ``vmod_dir``
