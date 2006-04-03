#!/bin/sh
#
# $Id$
#

if [ -d /usr/local/gnu-autotools/bin ] ; then
	PATH=${PATH}:/usr/local/gnu-autotools/bin
	export PATH
fi

base=$(cd $(dirname $0) && pwd)
for dir in $base $base/contrib/libevent ; do
	echo $dir
	cd $dir
	aclocal
	libtoolize --copy --force
	autoheader
	automake --add-missing --copy --force --foreign
	autoconf
done
