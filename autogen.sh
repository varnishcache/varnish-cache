#!/bin/sh
#
# $Id$
#

aclocal
autoheader
automake --add-missing --copy --force --foreign
autoconf
