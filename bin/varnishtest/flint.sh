#!/bin/sh

FLOPS='
	-DTOP_BUILDDIR="foo"
	-I../../lib/libvgz
	*.c
'

. ../../tools/flint_skel.sh

