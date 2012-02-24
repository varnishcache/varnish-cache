%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading from Varnish 2.1 to 3.0
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

This is a compilation of items you need to pay attention to when upgrading from Varnish 2.1 to 3.0

Changes to VCL
==============

In most cases you need to update your VCL since there has been some changes to the syntax.

string concatenation operator
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
String concatenation did not have an operator previously, but this has now been changed to ``+``.

no more %-escapes in strings
~~~~~~~~~~~~~~~~~~~~~~~~~~~~
To simplify strings, the %-encoding has been removed. If you need non-printable characters, you need to use inline C.

``log`` moved to the std vmod
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``log`` has moved to the std vmod::

	log "log something";

becomes::

	import std;
	std.log("log something");

You only need to import std once.

purges are now called bans
~~~~~~~~~~~~~~~~~~~~~~~~~~

``purge()`` and ``purge_url()`` are now respectively ``ban()`` and ``ban_url()``, so you should replace all occurences::

	purge("req.url = " req.url);

becomes::

	ban("req.url = " + req.url);

``purge`` does not take any arguments anymore, but can be used in vcl_hit or vcl_miss to purge the item from the cache, where you would reduce ttl to 0 in Varnish 2.1::

	sub vcl_hit {
	  if (req.request == "PURGE") {
	    set obj.ttl = 0s;
	    error 200 "Purged.";
	  }
	}

becomes::

	sub vcl_hit {
	  if (req.request == "PURGE") {
	    purge;
	    error 200 "Purged.";
	  }
	}

``beresp.cacheable`` and ``obj.cacheable`` are gone
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``beresp.cacheable`` is gone, and can be replaced with ``beresp.ttl > 0s``. Similarly ``obj.cacheable`` can be replaced with ``obj.ttl > 0s``.

returns are now done with the ``return()`` function
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``pass``, ``pipe``, ``lookup``, ``deliver``, ``fetch``, ``hash``, ``pipe`` and ``restart`` are no longer keywords, but arguments to ``return()``, so::

	sub vcl_pass {
	  pass;
	}

becomes::

	sub vcl_pass {
	  return(pass);
	}


``req.hash`` is replaced with ``hash_data()``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You no longer append to the hash with ``+=``, so::

	set req.hash += req.url;

becomes::

	hash_data(req.url);

``esi`` is replaced with ``beresp.do_esi``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You no longer enable ESI with ``esi``, so::

	esi;

in ``vcl_fetch`` becomes::

	set beresp.do_esi = true;

``pass`` in ``vcl_fetch`` renamed to ``hit_for_pass``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The difference in behaviour of ``pass`` in ``vcl_recv`` and
``vcl_fetch`` confused people, so to make it clearer that they are
different, you must now do ``return(hit_for_pass)`` when doing a pass
in ``vcl_fetch``.

Changes to runtime parameters
=============================

Deleted parameters
~~~~~~~~~~~~~~~~~~

``cache_vbe_conns`` and ``err_ttl`` has been removed.

New parameters
~~~~~~~~~~~~~~

The following parameters have been added, see man varnishd for reference:

* ``default_keep``
* ``expiry_sleep``
* ``fetch_maxchunksize``
* ``gzip_level``
* ``gzip_memlevel``
* ``gzip_stack_buffer``
* ``gzip_tmp_space``
* ``gzip_window``
* ``http_gzip_support``
* ``http_req_hdr_len``
* ``http_req_size``
* ``http_resp_hdr_len``
* ``http_resp_size``
* ``shortlived``
* ``thread_pool_workspace``
* ``vcc_err_unref``
* ``vcl_dir``
* ``vmod_dir``

Changed default values
~~~~~~~~~~~~~~~~~~~~~~

The following parameters have new defaults:

* ``ban_lurker_sleep`` changed from 0 to 0.01 seconds, enabling the ban lurker by default.
* ``connect_timeout`` changed from 0.4 to 0.7 seconds.
* ``log_hashstring`` changed from off to on.
* ``send_timeout`` changed from 60 to 60 seconds.
* ``thread_pool_add_delay`` changed from 20 to 2 ms.

Changed parameter names
~~~~~~~~~~~~~~~~~~~~~~~

The following parameters have new names:

* ``http_headers`` has been renamed to ``http_max_hdr``.
* ``max_esi_includes`` has been renamed to ``max_esi_depth``.
* ``overflow_max`` has been renamed to ``queue_max``.
* ``purge_dups`` has been renamed to ``ban_dups``.

Changes to behaviour
====================

Varnish will return an error when headers are too large instead of just ignoring them. If the limits are too low, Varnish will return HTTP 413. You can change the limits by increasing http_req_hdr_len and http_req_size.

thread_pool_max is now per thread pool, while it was a total across all pools in 2.1. If you had this set in 2.1, you should adjust it for 3.0.
