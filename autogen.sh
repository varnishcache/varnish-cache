#!/bin/sh
#
# $Id$
#

set -ex

if [ -d /usr/local/gnu-autotools/bin ] ; then
	PATH=/usr/local/gnu-autotools/bin:${PATH}
	export PATH
fi

aclocal
libtoolize --copy --force
autoheader
automake --add-missing --copy --foreign
autoconf
