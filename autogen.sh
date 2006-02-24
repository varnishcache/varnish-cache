#!/bin/sh
#
# $Id$
#

libtoolize --copy --force
aclocal
autoheader
automake --add-missing --copy --force --foreign
autoconf
