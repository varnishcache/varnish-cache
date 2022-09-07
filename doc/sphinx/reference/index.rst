..
	Copyright (c) 2010-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _reference-index:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%
The Varnish Reference Manual
%%%%%%%%%%%%%%%%%%%%%%%%%%%%

.. _reference-vcl:

The VCL language
----------------

.. toctree::
	:maxdepth: 1

	VCL - The Varnish Configuration Language <vcl>
	VCL Variables <vcl-var>
	VCL backend configuration <vcl-backend>
	VCL backend health probe <vcl-probe>
	states.rst

A collection of :ref:`vcl-design-patterns-index` is available in addition to
these reference manuals.

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
	CLI - The commands varnish understands <varnish-cli>

Logging and monitoring
----------------------

.. toctree::
	:maxdepth: 1

	VSL - The log records Varnish generates <vsl>
	VSLQ - Filter/Query expressions for VSL <vsl-query>
	VarnishLog - Logging raw VSL <varnishlog>
	VarnishNCSA - Logging in NCSA format <varnishncsa>
	VarnishHist - Realtime response histogram display <varnishhist>
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

For Developers & DevOps
-----------------------

.. toctree::
	:maxdepth: 1

	Shell tricks <shell_tricks>
	VMODS - Extensions to VCL <vmod>
	VEXT - Varnish Extensions <vext>
	VSM - Shared memory use <vsm>
	VDIR - Backends & Directors <directors>
	VCLI - CLI protocol API <cli_protocol>

.. Vmod_debug ?

.. Libvarnishapi

.. VRT

.. VRT compat levels

Code-book
---------

.. toctree::
	:maxdepth: 1

	vtla.rst
