#!/usr/bin/env python
#
# Copyright (c) 2006 Verdens Gang AS
# Copyright (c) 2006-2015 Varnish Software AS
# All rights reserved.
#
# Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# Generate vcs_version.h vmod_abi.h

from __future__ import print_function

import subprocess
import collections
import os
import sys

srcroot = "../.."
buildroot = "../.."
if len(sys.argv) == 3:
	srcroot = sys.argv[1]
	buildroot = sys.argv[2]
elif len(sys.argv) != 1:
	print("Two arguments or none")
	exit(2)

#######################################################################
def file_header(fo):
	fo.write("""/*
 * NB:  This file is machine generated, DO NOT EDIT!
 *
 * Edit and run include/generate.py instead.
 */

""")

#######################################################################


v = subprocess.check_output([
    "git --git-dir=%s rev-parse HEAD 2>/dev/null || echo NOGIT" %
    (os.path.join(srcroot, ".git"))
    ], shell=True, universal_newlines=True).strip()

vcsfn = os.path.join(srcroot, "include", "vcs_version.h")

try:
	i = open(vcsfn).readline()
except IOError:
	i = ""

ident = "/* " + v + " */\n"

if i != ident:
	fo = open(vcsfn, "w")
	fo.write(ident)
	file_header(fo)
	fo.write('#define VCS_Version "%s"\n' % v)
	fo.close()

	for i in open(os.path.join(buildroot, "Makefile")):
		if i[:14] == "PACKAGE_STRING":
			break
	i = i.split("=")[1].strip()

	fo = open(os.path.join(srcroot, "include", "vmod_abi.h"), "w")
	file_header(fo)
	fo.write('#define VMOD_ABI_Version "%s %s"\n' % (i, v))
	fo.close()
