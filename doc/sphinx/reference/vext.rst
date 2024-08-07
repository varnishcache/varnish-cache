..
	Copyright (c) 2010-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _ref-vext:

%%%%%%%%%%%%%%%%%%%%%%%%%
VEXT - Varnish Extensions
%%%%%%%%%%%%%%%%%%%%%%%%%

A Varnish Extension is a shared library, loaded into the worker
process during startup, before privileges are dropped for good.  This
allows a VEXT to do pretty much anything it wants to do in the
worker process.

A VEXT can (also) contain a VMOD, and it will work just like any
other VMOD, which also means that VMODs can be loaded as VEXTs.

The VEXTs are loaded in the child process, in the order they are
specified on the commandline, after the ``heritage`` has been
established, but before stevedores are initialized and jail
privileges are dropped.

There is currently no ``init`` entrypoint defined, but a
VEXT can use a static initializer to get activated on loading.

If those static initializers want to bail out, ``stderr`` and
``exit(3)`` can be used to convey diagnostics.
