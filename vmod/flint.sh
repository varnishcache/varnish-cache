#!/bin/sh
#
# Copyright (c) 2021 Varnish Software AS
# SPDX-License-Identifier: BSD-2-Clause
# See LICENSE file for full text of license

for vmod in vmod_*.vcc ; do
    vmod="${vmod%.vcc}"
    echo "====================="
    echo "${vmod}"
    echo "====================="
    vmod="${vmod#vmod_}"
    FLOPS="-I../bin/varnishd vcc_${vmod}_if.c vmod_${vmod}*.c" \
	 ../tools/flint_skel.sh
done
