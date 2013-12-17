.. _whatsnew_upgrading:

%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Varnish 4
%%%%%%%%%%%%%%%%%%%%%%

Changes to VCL
==============

Much of the VCL syntax has changed in Varnish 4. We've tried to compile a list of changes needed to upgrade here.

Version statement
~~~~~~~~~~~~~~~~~
To make sure that people have upgraded their VCL to the current version, varnish now requires the first line of VCL to indicate the VCL version number::

	vcl 4.0;

vcl_fetch is now vcl_backend_response
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Directors have been moved to the vmod_directors
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Use the hash director as a client director
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Since the client director was already a special case of the hash director, it has been removed, and you should use the hash director directly::

	sub vcl_init {
        	new h = directors.hash();
        	h.add_backend(b1, 1);
        	h.add_backend(b2, 1);
	}
	
	sub vcl_recv {
		set req.backend = h.backend(client.ip);
	}

error() is now a return value
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
You must now explicitly return an error::

	return(error(999, "Response));

hit_for_pass objects are created using beresp.uncacheable
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Example::

	sub vcl_backend_response {
		if(beresp.http.X-No-Cache) {
			set beresp.uncacheable = true;
			set beresp.ttl = 120s;
			return(deliver);
		}
	}

vcl_recv should return(hash) instead of lookup now
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

req.* not available in vcl_backend_response
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
req.* used to be available in vcl_fetch, but after the split of functionality, you only have bereq.* in vcl_backend_response.

vcl_* reserved
~~~~~~~~~~~~~~
Your own subs cannot be named vcl_* anymore. That is reserved for builtin subs.

req.backend.healthy replaced by std.healthy(req.backend)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Changes to parameters
=====================

linger
~~~~~~

sess_timeout
~~~~~~~~~~~~
