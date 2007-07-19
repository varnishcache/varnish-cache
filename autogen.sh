#!/bin/sh
#
# $Id$
#

warn() {
	echo "WARNING: $@" 1>&2
}

case `uname -s` in
Darwin)
	LIBTOOLIZE=glibtoolize
	;;
FreeBSD)
	LIBTOOLIZE=libtoolize
	if [ -d /usr/local/gnu-autotools/bin ] ; then
		PATH=/usr/local/gnu-autotools/bin:${PATH}
		export PATH
		FIX_BROKEN_FREEBSD_PORTS="-I /usr/local/share/aclocal"
	fi
	;;
Linux)
	LIBTOOLIZE=libtoolize
	;;
esac

automake_version=$(automake --version | tr ' ' '\n' | egrep '^[0-9]\.[0-9a-z.-]+')
if [ -z "$automake_version" ] ; then
    warn "unable to determine automake version"
else
    case $automake_version in
	0.*|1.[0-8]|1.[0-8][.-]*)
	    warn "automake ($automake_version) detected; 1.9 or newer recommended"
	    ;;
	*)
	    ;;
    esac
fi

set -ex

aclocal ${FIX_BROKEN_FREEBSD_PORTS}
$LIBTOOLIZE --copy --force
autoheader
automake --add-missing --copy --foreign
autoconf
