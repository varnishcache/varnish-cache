..
	Copyright (c) 2019-2021 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

This directory contains `coccinelle`_ semantic patches to facilitate code
maintenance.

Each patch should, in a comment section, explain its purpose. They may be fit
for both the in-tree code style or out-of-tree VMOD and VUT development.

Unless noted otherwise, all patches should work when invoked as::

	spatch --macro-file tools/coccinelle/vdef.h \
	       -I include/ -I bin/varnishd/ --dir . --in-place \
	       --sp-file $COCCI

To expand a patch and see the implicit rules that will be taken into account,
it is possible to parse the file::

	spatch --macro-file tools/coccinelle/vdef.h \
	       -I include/ -I bin/varnishd/ --parse-cocci
	       --sp-file $COCCI

The ``archive/`` directory contains patches which we used once and
should not need again, but want to retain for reference.

Do not commit any ``libvgz`` changes, as this code is manually kept in
sync with upstream.

.. _coccinelle: http://coccinelle.lip6.fr/
