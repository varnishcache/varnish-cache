#!/bin/sh
#
# Script to compare vgz with FreeBSD's copy of zlib
#
# Run this on a up-to-date FreeBSD source tree

for i in lib/libvgz/*.[ch]
do
	sed '
	s/"vgz.h"/"zlib.h"/
	s/msg = "/msg = (char *)"/
	' $i |
	    diff -u /usr/src/contrib/zlib/`basename $i` -
done
diff -u /usr/src/contrib/zlib/zlib.h lib/libvgz/vgz.h

