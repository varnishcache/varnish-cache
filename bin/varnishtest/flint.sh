#!/bin/sh
#
# Copyright (c) 2008-2021 Varnish Software AS
# SPDX-License-Identifier: BSD-2-Clause
# See LICENSE file for full text of license

FLOPS='
	-DVTEST_WITH_VTC_LOGEXPECT
	-DVTEST_WITH_VTC_VARNISH
	-DTOP_BUILDDIR="foo"
	-I../../lib/libvgz
	*.c
' ../../tools/flint_skel.sh
