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

Built-in VCL reference
----------------------

A copy of the ``builtin.vcl`` file might be provided with your Varnish
installation but :ref:`varnishd(1)` is the reference to determine the code
that is appended to any loaded VCL.

The VCL compilation happens in two passes:

- the first one compiles the built-in VCL only,
- and the second pass compiles the concatenation of the loaded and built-in
  VCLs.

Any VCL subroutine present in the built-in VCL can be extended, in which
case the loaded VCL code will be executed before the built-in code.
