#!/bin/sh
#
# $Id$
#

if [ -d /usr/local/gnu-autotools/bin ] ; then
	PATH=/usr/local/gnu-autotools/bin:${PATH}
	export PATH
fi

automake_version=$(automake --version | tr ' ' '\n' | egrep '^[0-9]\.[0-9a-z.-]+')
if [ -z "$automake_version" ] ; then
    echo "unable to determine automake version"
    exit 1
else
    case $automake_version in
	1.9*|[23456789].*)
	    ;;
	*)
	    echo "your version of automake ($automake_version) is too old;" \
		"you need 1.9 or newer."
	    exit 1
	    ;;
    esac
fi

set -ex

aclocal
libtoolize --copy --force
autoheader
automake --add-missing --copy --foreign
autoconf
