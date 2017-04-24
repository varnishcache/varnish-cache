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
	../../lib/libvarnish/flint.lnt
	../../lib/libvarnish/*.c
	../../lib/libvcc/flint.lnt
	../../lib/libvcc/*.c
	../../lib/libvmod_std/flint.lnt
	../../lib/libvmod_std/*.c
	../../lib/libvmod_debug/flint.lnt
	../../lib/libvmod_debug/*.c
	../../lib/libvmod_directors/flint.lnt
	../../lib/libvmod_directors/*.c
'

. ../../tools/flint_skel.sh
