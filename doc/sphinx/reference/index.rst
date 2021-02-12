.. _reference-index:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%
The Varnish Reference Manual
%%%%%%%%%%%%%%%%%%%%%%%%%%%%

The VCL language
----------------

.. toctree::
	:maxdepth: 1

	VCL - Varnish Configuration Language <vcl>
	states.rst

VCL Design Patterns
-------------------

.. toctree::
	:maxdepth: 1

	dp_vcl_recv_hash.rst

Bundled VMODs
-------------

.. toctree::
	:maxdepth: 1

	vmod_blob.rst
	vmod_cookie.rst
	vmod_directors.rst
	vmod_proxy.rst
	vmod_purge.rst
	vmod_std.rst
	vmod_unix.rst

The CLI interface
-----------------

.. toctree::
	:maxdepth: 1

	VarnishAdm - Control program for Varnish <varnishadm>
	VCLI - The commands varnish understands <varnish-cli>

Logging and monitoring
----------------------

.. toctree::
	:maxdepth: 1

	VSL - The log records Varnish generates <vsl>
	VSLQ - Filter/Query expressions for VSL <vsl-query>
	VarnishLog - Logging raw VSL <varnishlog>
	VarnishNCSA - Logging in NCSA format <varnishncsa>
	VarnishHist - Realtime reponse histogram display <varnishhist>
	VarnishTop - Realtime activity display <varnishtop>
	
Counters and statistics
-----------------------

.. toctree::
	:maxdepth: 1

	VSC - The statistics Varnish collects <varnish-counters>
	VarnishStat - Watching and logging statistics <varnishstat>

The Varnishd program
--------------------

.. toctree::
	:maxdepth: 1

	VarnishD - The program which does the actual work <varnishd>

Varnishtest
-----------

.. toctree::
	:maxdepth: 1

	VTC - Language for writing test cases <vtc>
	VarnishTest - execute test cases <varnishtest>
	vmod_vtc.rst

For Developers
--------------

.. toctree::
	:maxdepth: 1

	VMODS - Extensions to VCL <vmod>
	VSM - Shared memory use <vsm>
	VDIR - Backends & Directors <directors>

.. Vmod_debug ?

.. Libvarnishapi

.. VRT

.. VRT compat levels

Code-book
---------

.. toctree::
	:maxdepth: 1

	vtla.rst
