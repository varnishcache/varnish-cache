.. _whatsnew_upgrading_5_0:

%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 5.0
%%%%%%%%%%%%%%%%%%%%%%%%

Changes to VCL
==============

* All VCL Objects should now be defined before used

  * in particular, this is now required for ACLs. The error message
    for ACLs being used before being defined is confusing - see PR #2021::

	Name <acl> is a reserved name

* VCL names are restricted to alphanumeric characters, dashes (-) and
  underscores (_).  In addition, the first character should be alphabetic.
  That is, the name should match "[A-Za-z][A-Za-z0-9\_-]*".

* Backend and integer types can be used in a boolean context.
  See ``vcl(7)`` for details.

* Add support to perform matches in assignments, obtaining a boolean
  as result::

        set req.http.foo = req.http.bar ~ "bar";

* Returned values from functions and methods' calls can be thrown away.

backends
~~~~~~~~

* Added support for the PROXY protocol via ``.proxy_header`` attribute.
  Possible values are 1 and 2, corresponding to the PROXY protocol
  version 1 and 2, respectively.

vcl_recv
~~~~~~~~

* Added ``return (vcl(label))`` to switch to the VCL labelled `label`.
* The ``rollback`` function has been retired.

vcl_hit
~~~~~~~

* Replace ``return (fetch)`` with ``return (miss)``.

vcl_backend_*
~~~~~~~~~~~~~

* Added read access to ``remote.ip``, ``client.ip``, ``local.ip`` and
  ``server.ip``.

vcl_backend_fetch
~~~~~~~~~~~~~~~~~

* Added write access to ``bereq.body``, the request body. Only ``unset``
  is supported at this time.

* We now send request bodies by default (see :ref:_whatsnew_changes_5.0).
  To keep the previous behaviour add the following code before any
  ``return (..)`` statement in this subroutine::

	if (bereq.method == "GET") {
	    unset bereq.body;
	}


vcl_backend_error
~~~~~~~~~~~~~~~~~

* Added write access to ``beresp.body``, the response body. This may
  replace ``synthetic()`` in future releases.

vcl_deliver
~~~~~~~~~~~

* Added read access to ``obj.ttl``, ``obj.age``, ``obj.grace`` and
  ``obj.keep``.

vcl_synth
~~~~~~~~~

* Added write access to ``resp.body``, the response body. This may replace
  ``synthetic()`` in future releases.

Management interface
====================

* To disable CLI authentication use ``-S none``.

* ``n_waitinglist`` statistic removed.

Changes to parameters
=====================

* Added ``ban_lurker_holdoff``.

* Removed ``session_max``.  This parameter actually had no effect since
  4.0 but might come back in a future release.

* ``vcl_path`` is now a colon-separated list of directories, replacing
  ``vcl_dir``.

* ``vmod_path`` is now a colon-separated list of directories, replacing
  ``vmod_dir``.
