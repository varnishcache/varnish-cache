#!/usr/bin/env python
#-
# Copyright (c) 2006 Verdens Gang AS
# Copyright (c) 2006-2014 Varnish Software AS
# All rights reserved.
#
# Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
# Author: Martin Blix Grydeland <martin@varnish-software.com>
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
# Generate various .c and .h files for the VSL query expression parser
# and the interfaces for it.

import sys
import copy

srcroot = "../.."
buildroot = "../.."
if len(sys.argv) == 3:
        srcroot = sys.argv[1]
        buildroot = sys.argv[2]

#######################################################################
# These are our tokens

tokens = {
        # Numerical comparisons
        "T_EQ":         "==",
        "T_NEQ":        "!=",
        "T_LEQ":        "<=",
        "T_GEQ":        ">=",

        # String comparisons
        "T_SEQ":        "eq",
        "T_SNEQ":       "ne",

        # Regular expression matching
        "T_NOMATCH":    "!~",

        # Boolean operators
        "T_AND":        "and",
        "T_OR":         "or",
        "T_NOT":        "not",

        # Miscellaneous
        None:           "<>~[]{}():,",

        # These have handwritten recognizers
        "VAL":          None,
        "EOI":          None,

        # Special
        "T_TRUE":       None,
}

#######################################################################
# Emit a function to recognize tokens in a string

def emit_vxp_fixed_token(fo, tokens):
        recog = list()
        emit = dict()
        for i in tokens:
                j = tokens[i]
                if (j != None):
                        recog.append(j)
                        emit[j] = i

        recog.sort()
        rrecog = copy.copy(recog)
        rrecog.sort(key = lambda x: -len(x))

        fo.write("""
unsigned
vxp_fixed_token(const char *p, const char **q)
{

\tswitch (p[0]) {
""")
        last_initial = None
        for i in recog:
                if (i[0] == last_initial):
                        continue
                last_initial = i[0]
                fo.write("\tcase '%s':\n" % last_initial)
                for j in rrecog:
                        if (j[0] != last_initial):
                                continue

                        fo.write("\t\tif (")
                        k = 1
                        l = len(j)
                        while (k < l):
                                fo.write("p[%d] == '%s'" % (k, j[k]))
                                fo.write(" &&\n\t\t    ")
                                k += 1
                        fo.write("(isword(p[%d]) ? !isword(p[%d]) : 1)) {\n" %
                                 (l - 1, l))
                        fo.write("\t\t\t*q = p + %d;\n" % l)
                        fo.write("\t\t\treturn (%s);\n" % emit[j])
                        fo.write("\t\t}\n");
                fo.write("\t\treturn (0);\n")

        fo.write("\tdefault:\n\t\treturn (0);\n\t}\n}\n")

#######################################################################
# Emit the vxp_tnames (token->string) conversion array

def emit_vxp_tnames(fo, tokens):
        fo.write("\nconst char * const vxp_tnames[256] = {\n")
        l = list(tokens.keys())
        l.sort()
        for i in l:
                j = tokens[i]
                if j == None:
                        j = i
                if i[0] == "'":
                        j = i
                fo.write("\t[%s] = \"%s\",\n" % (i, j))
        fo.write("};\n")

#######################################################################

def polish_tokens(tokens):
        # Expand single char tokens
        st = tokens[None]
        del tokens[None]

        for i in st:
                tokens["'" + i + "'"] = i

#######################################################################

def file_header(fo):
        fo.write("""/*
 * NB:  This file is machine generated, DO NOT EDIT!
 *
 * Edit and run generate.py instead
 */
""")

#######################################################################

polish_tokens(tokens)

fo = open(buildroot + "/lib/libvarnishapi/vxp_tokens.h", "w")

file_header(fo)

j = 128
l = list(tokens.keys())
l.sort()
for i in l:
        if i[0] == "'":
                continue
        fo.write("#define\t%s %d\n" % (i, j))
        j += 1
        assert j < 256

fo.close()

#######################################################################

fo = open(buildroot + "/lib/libvarnishapi/vxp_fixed_token.c", "w")

file_header(fo)
fo.write("""

#include "config.h"

#include <ctype.h>
#include <stdio.h>

#include "vxp.h"
""")

emit_vxp_fixed_token(fo, tokens)
emit_vxp_tnames(fo, tokens)

fo.close()
