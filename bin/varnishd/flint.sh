#!/bin/sh

FLOPS='
	-I../../lib/libvgz
	-DNOT_IN_A_VMOD
	-DVARNISH_STATE_DIR="foo"
	-DVARNISH_VMOD_DIR="foo"
	-DVARNISH_VCL_DIR="foo"
	cache/*.c
	common/*.c
	hash/*.c
	http1/*.c
	http2/*.c
	mgt/*.c
	proxy/*.c
	storage/*.c
	waiter/*.c
	../../lib/libvarnish/flint.lnt
	../../lib/libvarnish/*.c
	../../lib/libvcc/flint.lnt
	../../lib/libvcc/*.c
	../../vmod/flint.lnt
	../../vmod/*.c
'

. ../../tools/flint_skel.sh
