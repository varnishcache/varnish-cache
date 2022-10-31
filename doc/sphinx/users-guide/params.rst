..
	Copyright (c) 2012-2017 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license



Parameters
----------

Varnish Cache comes with a set of parameters that affects behaviour and
performance. Parameters are set either though command line
arguments to ``varnishd`` or at runtime through ``varnishadm`` using
the ``param.set`` CLI command.

We don't recommend that you tweak parameters unless you're sure of what
you're doing. We've worked hard to make the defaults sane and Varnish
should be able to handle most workloads with the default settings.

For a complete listing of all parameters and their specifics see
:ref:`varnish-params(7)`.
