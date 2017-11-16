#!/bin/sh

# This script compares libvgz to zlib in FreeBSD source tree

LZ=/usr/src/contrib/zlib

if [ ! -d lib/libvgz ] ; then
	echo "Run this from to of tree" 1>&2
	exit 2
fi

for i in lib/libvgz/*.[ch]
do
	b=`basename $i`
	if [ "$b" == "vgz.h" ] ; then
		b="zlib.h"
	fi
	if [ -f ${LZ}/$b ] ; then
		echo "==== $b"
		sed '
		s/vgz.h/zlib.h/
		/strm->msg =/s/"/(char *)"/
		' $i |
		diff -u ${LZ}/$b -
	else
		echo "#### $b #### NOT FOUND ####"
	fi
done
