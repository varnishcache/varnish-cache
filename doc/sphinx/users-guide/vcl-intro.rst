Varnish Configuration Language - VCL
-------------------------------------

Varnish has a great configuration system. Most other systems use
configuration directives, where you basically turn on and off lots of
switches. Varnish uses a domain specific language called Varnish
Configuration Language, or VCL for short. Varnish translates this
configuration into binary code which is then executed when requests
arrive.

The VCL files are divided into subroutines. The different subroutines
are executed at different times. One is executed when we get the
request, another when files are fetched from the backend server.

Varnish will execute these subroutines of code at different stages of
its work. At some point you call an action in this subroutine and then
the execution of that subroutine stops.

If you don't call an action in your subroutine and it reaches the end
Varnish will execute the built in VCL code. You will see this VCL
code commented out in default.vcl.

99% of all the changes you'll need to do will be done in two of these
subroutines. *vcl_recv* and *vcl_backend_response*.

.. _users-guide-vcl_fetch_actions:


