.. _vcl-built-in-code:

Built-in VCL
============

Whenever a VCL program is loaded, the built-in VCL is appended to it. The
vcl built-in subs (:ref:`vcl_steps`) have a special property, they can appear multiple
times and the result is concatenation of all built-in subroutines.

For example, let's take the following snippet::

    sub vcl_recv {
        # loaded code for vcl_recv
    }

The effective VCL that is supplied to the compiler looks like::

    sub vcl_recv {
        # loaded code for vcl_recv
        # built-in code for vcl_recv
    }

This is how it is guaranteed that all :ref:`reference-states` have at least
one ``return (<action>)``.

It is generally recommended not to invariably return from loaded code to
let Varnish execute the built-in code, because the built-in code provides
essentially a sensible default behavior for an HTTP cache.

Built-in subroutines split
--------------------------

It might however not always be practical that the built-in VCL rules take
effect at the very end of a state, so some subroutines like ``vcl_recv``
are split into multiple calls to other subroutines.

By convention, those assistant subroutines are named after the variable
they operate on, like ``req`` or ``beresp``. This allows for instance to
circumvent default behavior.

For example, ``vcl_recv`` in the built-in VCL prevents caching when clients
have a cookie. If you can trust your backend to always specify whether a
response is cacheable or not regardless of whether the request contained a
cookie you can do this::

    sub vcl_req_cookie {
        return;
    }

With this, all other default behaviors from the built-in ``vcl_recv`` are
executed and only cookie handling is affected.

Another example is how the built-in ``vcl_backend_response`` treats a
negative TTL as a signal not to cache. It's a historical mechanism to mark
a response as uncacheable, but only if the built-in ``vcl_backend_response``
is not circumvented by a ``return (<action>)``.

However, in a multi-tier architecture where a backend might be another
Varnish server, you might want to cache stale responses to allow the
delivery of graced objects and enable revalidation on the next fetch. This
can be done with the following snippet::

    sub vcl_beresp_stale {
        if (beresp.ttl + beresp.grace > 0s) {
            return;
        }
    }

This granularity, and the general goal of the built-in subroutines split
is to allow to circumvent a specific aspect of the default rules without
giving the entire logic up.

Specific split built-in subroutines
-----------------------------------

Some split subroutines in the built-in VCL deserve additional explanations:

.. _vcl-built-in-refresh:

vcl_refresh_*
~~~~~~~~~~~~~

These subroutines handle edge cases of backend refreshes. The precondition for
these to be entered implicitly or explicitly via ``vcl_backend_refresh`` is that
the current backend request can potentially create a cache object (that is, it
is not for a private object as created by a pass or hit-for-pass) and that a
stale object was found in cache which is not already invalidated. If this is the
case, core code constructs a conditional ``GET`` request with the
``If-Modified-Since`` and/or ``If-None-Match`` headers set before
``vcl_backend_fetch`` is entered. If the VCL code does not remove the headers,
the backend might respond with a ``304 Not Modified`` status, in which case
``vcl_backend_refresh`` is called on the response to decide what do do (see
:ref:`vcl_backend_refresh` for reference) and, if the built-in VCL is reached,
the subs documented below will be called via ``vcl_builtin_backend_refresh``.

vcl_refresh_valid
~~~~~~~~~~~~~~~~~

``vcl_refresh_valid`` handles the case where the stale object to be revalidated
by the 304 response got explicitly removed from the cache by a ban or purge
while the backend request was in progress::

	sub vcl_refresh_valid {
		if (!obj_stale.is_valid) {
			return (error(503, "Invalid object for refresh"));
		}
	}

The error is generated because alternative actions might require additional
consideration. There are basically two options:

We can ignore the fact that the now successfully revalidated object was *just*
invalidated by not falling through to the built-in VCL with this subroutine in
the user VCL::

	sub vcl_refresh_valid {
		return;
	}

This avoids the error but can potentially result in invalidations being
ineffective.

The other option is to retry the backend request without the conditional request
headers. This option is implicitly active whenever the user VCL results in a
``return(retry)`` from ``vcl_backend_error``, because core code removes the
conditional request headers if the stale object is found to be invalidated.

A variant of this option is an explicit retry for the case at hand::

	sub vcl_refresh_valid {
                return (retry);
        }

To summarize, refreshes should work fine as long as there is at least one retry
from ``vcl_backend_error`` for 503 errors. Additionally, VCL allows for
customization if needed.

vcl_refresh_conditions
~~~~~~~~~~~~~~~~~~~~~~

This sub safeguards against invalid 304 responses getting unnoticed::

	sub vcl_refresh_conditions {
		if (!bereq.http.if-modified-since &&
		    !bereq.http.if-none-match) {
			return (error(503, "Unexpected 304"));
		}
	}

A backend should not respond with a 304 if neither of the conditional request
headers were present in the backend request.

vcl_refresh_status
~~~~~~~~~~~~~~~~~~

This sub safeguards against accidental 304 responses if the stale object does
not have a 200 status::

	sub vcl_refresh_status {
		if (obj_stale.status != 200) {
			return (error(503, "Invalid object for refresh (status)"));
		}
	}

The background here is that the HTTP standards only allow refreshes of status
200 objects, but Vinyl Cache core code allows to deliberately violate this. In
such cases, the status check needs to be neutered by not running the built-in
code using::

	sub vcl_refresh_status {
                return;
        }

Built-in VCL reference
----------------------

A copy of the ``builtin.vcl`` file can be obtained by running
``varnishd -x builtin``.

The VCL compilation happens in two passes:

- the first one compiles the built-in VCL only,
- and the second pass compiles the concatenation of the loaded and built-in
  VCLs.

Any VCL subroutine present in the built-in VCL can be extended, in which
case the loaded VCL code will be executed before the built-in code.

Re-enabling pipe mode
~~~~~~~~~~~~~~~~~~~~~

As of Varnish 8.0, Varnish no longer pipes unknown HTTP methods by default.
Instead, it returns a 501 synthetic error. If you want to re-enable pipe
mode for a specific method, you can do so by adding the following to your
VCL:

.. code-block:: vcl

    sub vcl_req_method {
            if (req.method == "CUSTOM") {
                    return (pipe);
            }
    }

You can also re-enable pipe mode for a specific request, for example for
WebSockets:

.. code-block:: vcl

    sub vcl_recv {
            if (req.http.upgrade == "websocket") {
                    return (pipe);
            }
    }
