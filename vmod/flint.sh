#!/bin/sh

for vmod in vmod_*.vcc ; do
    vmod="${vmod%.vcc}"
    vmod="${vmod#vmod_}"
    echo "====================="
    echo "${vmod}"
    echo "====================="
    FLOPS="-I../bin/varnishd vcc_${vmod}_if.c vmod_${vmod}*.c" \
	 ../tools/flint_skel.sh
done
