#!/bin/sh

FLOPS='
	-DVTEST_WITH_VTC_LOGEXPECT
	-DVTEST_WITH_VTC_VARNISH
	-DTOP_BUILDDIR="foo"
	-I../../lib/libvgz
	*.c
' ../../tools/flint_skel.sh
