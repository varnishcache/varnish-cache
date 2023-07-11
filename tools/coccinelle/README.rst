..
	Copyright (c) 2019-2022 Varnish Software AS
	Copyright 2023 UPLEX - Nils Goroll Systemoptimierung
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

THE EASY PART
=============

.. _coccinelle: http://coccinelle.lip6.fr/

This directory contains `coccinelle`_ semantic patches to facilitate code
maintenance.

Each patch should, in a comment section, explain its purpose. They may be fit
for both the in-tree code style or out-of-tree VMOD and VUT development.

For in-tree usage, see the ``vcocci.sh`` script for convenience.

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

THE HARD PART
=============

*or: a word of warning.*

Coccinelle does not like our way of using macros for code generation,
but it will silently ignore anything it does not understand, so it
will not cause any harm.

Except when it does. Ruin your day.

Because you wonder why code which clearly should match on a semantic
patch does not.

Take this example, which motivated this section to be written::

  @@
  type T;
  T e1, e2;
  @@

  - vmin_t(T, e1, e2)
  + vmin(e1, e2)

simple as can be: For a type ``T``, replace ``vmin_t(T, ..., ...)`` for
expressions which are also of type ``T``.

Yet, why does it not change this code ...

::

   static int
   vcl_acl_cmp(const struct acl_e *ae1, const struct acl_e *ae2)
   {
     // ...
     m = vmin_t(unsigned, ae1->mask, ae2->mask);

when it does in fact change similar places.

Reason: It could not parse the ``struct acl_e`` declaration, so it
could not determine that the ``mask`` member is in fact of type
``unsigned``.

Check for parse errors
----------------------

In such cases, *do* check for parse errors in the affected file using
``spatch --parse-c``. Here's a one-liner to check the whole tree::

  for file in $(find . -name \*.c) ; do
    if spatch --macro-file tools/coccinelle/vdef.h \
       -I include/ -I bin/varnishd/  --parse-c $file 2>&1 |
       grep -C 5 -E '^BAD' ; then
         echo ; echo $file
    fi
  done

There are many cases for which no obvious workaround exists yet, but
some have been added to ``tools/coccinelle/vdef.h``.

Good lug with the ladybug.
