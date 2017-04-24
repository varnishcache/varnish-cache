#!/bin/sh

FLOPS='
	-I../../lib/libvgz
	-DVARNISHD_IS_NOT_A_VMOD
	-DVARNISH_STATE_DIR=\"foo\"
	-DVARNISH_VMOD_DIR=\"foo\"
	-DVARNISH_VCL_DIR=\"foo\"
	cache/*.c
	common/*.c
	hash/*.c
	http1/*.c
	http2/*.c
	mgt/*.c
	proxy/*.c
	storage/*.c
	waiter/*.c
	../../lib/libvarnish/*.c
	../../lib/libvcc/*.c
	../../lib/libvmod_std/*.c
	../../lib/libvmod_debug/*.c
	../../lib/libvmod_directors/*.c
'

. ../../tools/flint_skel.sh
