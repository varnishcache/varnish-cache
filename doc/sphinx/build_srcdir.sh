#!/bin/sh
#
# Written by Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
#
# This file is in the public domain.
#
# Usage: ./build_srcdir.sh destdir srcdir...
#
# Recursively copy the contents of srcdir into destdir, where srcdir
# arguments are listed from highest to lowest precedence.

set -e
set -u

destdir=$1
shift

for srcdir
do
	b=$(basename "$srcdir")
	find "$srcdir"/* |
	while read f
	do
		r=${f#$srcdir/}
		test -n "${r%%build*}" || continue
		d=$(dirname "$r")
		mkdir -p "$destdir/$b/$d"
		if [ -f "$f" ] && ! [ -f "$destdir/$b/$r" ]
		then
			cp $f "$destdir/$b/$r"
		fi
	done
done
