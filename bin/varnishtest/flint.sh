#!/bin/sh

FLOPS='
	-DTOP_BUILDDIR="foo"
	-I../../lib/libvgz
	*.c
	teken/teken.c
'

. ../../tools/flint_skel.sh

