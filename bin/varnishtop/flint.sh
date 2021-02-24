#!/bin/sh
#
# Copyright (c) 2010-2021 Varnish Software AS
# SPDX-License-Identifier: BSD-2-Clause
# See LICENSE file for full text of license

FLOPS='
	*.c
	../../lib/libvarnishapi/flint.lnt
	../../lib/libvarnishapi/*.c
' ../../tools/flint_skel.sh
