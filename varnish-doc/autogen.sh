#!/bin/sh
#
# $Id$
#

aclocal
automake --add-missing --copy --force --foreign
autoconf
