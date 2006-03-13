#!/bin/sh
#
# $Id$
#

if [ -d /usr/local/gnu-autotools/bin ] ; then
	PATH=${PATH}:/usr/local/gnu-autotools/bin
	export PATH
fi

aclocal
libtoolize --copy --force
autoheader
automake --add-missing --copy --force --foreign
autoconf
