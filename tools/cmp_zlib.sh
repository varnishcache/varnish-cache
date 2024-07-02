#!/bin/sh

# Compare libvgz with github/madler/zlib

LZ=/tmp/zlib

if [ "${LZ}" = "/tmp/zlib" -a ! -d ${LZ} ] ; then
    rm -rf ${LZ}
    git clone https://github.com/madler/zlib ${LZ}
else
    (cd ${LZ} && git pull)
fi

for i in lib/libvgz/*.[ch]
do
	b=`basename $i`
	if [ "$b" == "vgz.h" ] ; then
		b="zlib.h"
	fi
	if [ -f ${LZ}/$b ] ; then
		echo "#################################### $b"
		sed '
		s/vgz.h/zlib.h/
		# /strm->msg =/s/"/(char *)"/
		' $i |
		diff -wu - ${LZ}/$b
	else
		echo "#### $b #### NOT FOUND ####"
	fi
done
