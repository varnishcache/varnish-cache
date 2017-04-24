#!/bin/sh

FLOPS='
	-DVARNISH_STATE_DIR=\"foo\"
	*.c
	../../lib/libvarnishapi/flint.lnt
	../../lib/libvarnishapi/*.c
'

. ../../tools/flint_skel.sh

