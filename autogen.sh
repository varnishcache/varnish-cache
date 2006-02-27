#!/bin/sh
#
# $Id$
#

aclocal
libtoolize --copy --force
autoheader
automake --add-missing --copy --force --foreign
autoconf
