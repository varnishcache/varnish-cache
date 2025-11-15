#!/bin/sh

set -e

#echo "##TD## $*" 1>&2

if [ "x$1" != "x--top-srcdir" ] ; then exit 4 ; fi
top_srcdir=$2
shift ; shift

if [ "x$1" != "x--top-builddir" ] ; then exit 4 ; fi
top_builddir=$2
shift ; shift

for i in ${top_builddir}/bin/*
do
	if [ -d $i ] ; then
		PATH=${i}:${PATH}
	fi
done

export PATH

VMOD_PATH="`cd ${top_builddir} && pwd`/vmod/.libs"
VINYL_TEST_ARGS="-p vmod_path=${VMOD_PATH}"
export VINYL_TEST_ARGS

exec ${top_srcdir}/pretend_vtest/bin/vtest \
	--extension \
		${top_builddir}/lib/libvtest_ext_vinyl/.libs/libvtest_ext_vinyl.so \
	$*
