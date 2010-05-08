%%%%%%%%%%%%%%%
Configuration
%%%%%%%%%%%%%%%

VCL
===

**What is VCL?**

VCL is an acronym for Varnish Configuration Language.  In a VCL file, you configure how Varnish should behave.  Sample VCL files will be included in this Wiki at a later stage.

**Where is the documentation on VCL?**

We are working on documenting VCL. The `WIKI <http://varnish-cache.org/wiki/VCLExamples>`_ contains some examples.

Please also see "man 7 vcl".


**How do I load VCL file while Varnish is running?**

* Place the VCL file on the server
* Telnet into the managment port.
* do a "vcl.load <configname> <filename>" in managment interface. <configname> is whatever you would like to call your new configuration.
* do a "vcl.use <configname>" to start using your new config.

