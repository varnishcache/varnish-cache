%%%%%%%%%%%%%%%
Configuration
%%%%%%%%%%%%%%%

.. _faq-vcl:

VCL
===

**What is VCL?**

VCL is an acronym for Varnish Configuration Language.  In a VCL file, you configure how Varnish should behave.  Sample VCL files will be included in this Wiki at a later stage.

**Where is the documentation on VCL?**

We are working on documenting VCL. The `WIKI <http://varnish-cache.org/wiki/VCLExamples>`_ contains some examples.

Please also see ``man 7 vcl``.


**How do I load VCL file while Varnish is running?**

* Place the VCL file on the server
* Telnet into the managment port.
* do a "vcl.load <configname> <filename>" in managment interface. <configname> is whatever you would like to call your new configuration.
* do a "vcl.use <configname>" to start using your new config.

**Should I use ''pipe'' or ''pass'' in my VCL code? What is the difference?**

When varnish does a ``pass`` it acts like a normal HTTP proxy. It
reads the request and pushes it onto the backend. The next HTTP
request can then be handled like any other.

``pipe`` is only used when Varnish for some reason can't handle the
``pass``. ``pipe`` reads the request, pushes in onty the backend
_only_ pushes bytes back and forth, with no other actions taken.

Since most HTTP clients do pipeline several requests into one
connection this might give you an undesirable result - as every
subsequent request will reuse the existing ``pipe``.

Varnish versions prior to 2.0 does not support handling a request body
with ``pass`` mode, so in those releases ``pipe`` is required for
correct handling.

In 2.0 and later, ``pass`` will handle the request body correctly.

If you get 503 errors when making a request which is ``pass`` ed, make sure
that you're specifying the backend before returning from vcl_recv with ``pass``.



