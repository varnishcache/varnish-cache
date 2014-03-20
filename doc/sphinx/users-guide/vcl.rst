.. _users_vcl:

VCL - Varnish Configuration Language
------------------------------------

This section covers how to tell Varnish how to handle
your HTTP traffic, using the Varnish Configuration Language (VCL).

Varnish has a great configuration system. Most other systems use
configuration directives, where you basically turn on and off lots of
switches. We have instead chosen to use a domain specific language called VCL for this.

Every inbound request flows through Varnish and you can influence how
the request is being handled by altering the VCL code. You can direct
certain requests to certains backends, you can alter the requests and
the responses or have Varnish take various actions depending on
arbitrary properties of the request or the response. This makes
Varnish an extremely powerful HTTP processor, not just for caching.

Varnish translates VCL into binary code which is then executed when
requests arrive. The performance impact of VCL is negligible.

The VCL files are organized into subroutines. The different subroutines
are executed at different times. One is executed when we get the
request, another when files are fetched from the backend server.

If you don't call an action in your subroutine and it reaches the end
Varnish will execute some built-in VCL code. You will see this VCL
code commented out in the file `builtin.vcl` that ships with Varnish Cache.

.. _users-guide-vcl_fetch_actions:

.. toctree::
   :maxdepth: 1

   vcl-syntax
   vcl-built-in-subs
   vcl-variables
   vcl-actions
   vcl-backends
   vcl-hashing
   vcl-grace
   vcl-inline-c
   vcl-examples
   devicedetection

