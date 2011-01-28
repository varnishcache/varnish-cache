#!/usr/local/bin/python3.1
#-
# Copyright (c) 2006 Verdens Gang AS
# Copyright (c) 2006-2010 Linpro AS
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
# Generate various .c and .h files for the VCL compiler and the interfaces
# for it.
#
# $Id$

#######################################################################
# These are our tokens

# We could drop all words such as "include", "if" etc, and use the
# ID type instead, but declaring them tokens makes them reserved words
# which hopefully makes for better error messages.
# XXX: does it actually do that ?

import sys

srcroot = "../.."
buildroot = "../.."
if len(sys.argv) == 3:
	srcroot = sys.argv[1]
	buildroot = sys.argv[2]

tokens = {
	"T_INC":	"++",
	"T_DEC":	"--",
	"T_CAND":	"&&",
	"T_COR":	"||",
	"T_LEQ":	"<=",
	"T_EQ":		"==",
	"T_NEQ":	"!=",
	"T_GEQ":	">=",
	"T_SHR":	">>",
	"T_SHL":	"<<",
	"T_INCR":	"+=",
	"T_DECR":	"-=",
	"T_MUL":	"*=",
	"T_DIV":	"/=",
	"T_NOMATCH":	"!~",
	"T_INCLUDE":	"include",
	"T_IF":		"if",
	"T_ELSEIF":	"elseif",
	"T_ELSIF":	"elsif",
	"T_ELSE":	"else",

	# Single char tokens, for convenience on one line
	None:		"{}()*+-/%><=;!&.|~,",

	# These have handwritten recognizers
	"ID":		None,
	"CNUM":		None,
	"CSTR":		None,
	"EOI":		None,
	"CSRC":		None,
}

#######################################################################
# Our methods and actions

returns =(
	('recv',	('error', 'pass', 'pipe', 'lookup',)),
	('pipe',	('error', 'pipe',)),
	('pass',	('error', 'restart', 'pass',)),
	('hash',	('hash',)),
	('miss',	('error', 'restart', 'pass', 'fetch',)),
	('hit',		('error', 'restart', 'pass', 'deliver',)),
	('fetch',	('error', 'restart', 'pass', 'deliver',)),
	('deliver',	('restart', 'deliver',)),
	('error',	('restart', 'deliver',)),
)

#######################################################################
# Variables available in sessions

sp_variables = (
	('client.ip',
		'IP',
		( 'all',),
		( ),
		'const struct sess *'
	),
	('client.identity',
		'STRING',
		( 'all',),
		( 'all',),
		'struct sess *'
	),
	('server.ip',
		'IP',
		( 'all',),
		( ),
		'struct sess *'
	),
	('server.hostname',
		'STRING',
		( 'all',),
		( ),
		'struct sess *'
	),
	('server.identity',
		'STRING',
		( 'all',),
		( ),
		'struct sess *'
	),
	('server.port',
		'INT',
		( 'all',),
		( ),
		'struct sess *'
	),
	('req.request',
		'STRING',
		( 'all',),
		( 'all',),
		'const struct sess *'
	),
	('req.url',
		'STRING',
		( 'all',),
		( 'all',),
		'const struct sess *'
	),
	('req.proto',
		'STRING',
		( 'all',),
		( 'all',),
		'const struct sess *'
	),
	('req.http.',
		'HDR_REQ',
		( 'all',),
		( 'all',),
		'const struct sess *'
	),
	('req.backend',
		'BACKEND',
		( 'all',),
		( 'all',),
		'struct sess *'
	),
	('req.restarts',
		'INT',
		( 'all',),
		( ),
		'const struct sess *'
	),
	('req.esi_level',
		'INT',
		( 'all',),
		( ),
		'const struct sess *'
	),
	('req.grace',
		'DURATION',
		( 'all',),
		( 'all',),
		'struct sess *'
	),
	('req.xid',
		'STRING',
		( 'all',),
		( ),
		'struct sess *'
	),
	('req.esi',
		'BOOL',
		( 'recv', 'fetch', 'deliver', 'error',),
		( 'recv', 'fetch', 'deliver', 'error',),
		'struct sess *'
	),
	('req.can_gzip',
		'BOOL',
		( 'all',),
		( ),
		'struct sess *'
	),
	('req.backend.healthy',
		'BOOL',
		( 'all',),
		( ),
		'const struct sess *'
	),
	('req.hash_ignore_busy',
		'BOOL',
		( 'recv',),
		( 'recv',),
		'struct sess *'
	),
	('req.hash_always_miss',
		'BOOL',
		( 'recv',),
		( 'recv',),
		'struct sess *'
	),
	('bereq.request',
		'STRING',
		( 'pipe', 'pass', 'miss', 'fetch',),
		( 'pipe', 'pass', 'miss', 'fetch',),
		'const struct sess *'
	),
	('bereq.url',
		'STRING',
		( 'pipe', 'pass', 'miss', 'fetch',),
		( 'pipe', 'pass', 'miss', 'fetch',),
		'const struct sess *'
	),
	('bereq.proto',
		'STRING',
		( 'pipe', 'pass', 'miss', 'fetch',),
		( 'pipe', 'pass', 'miss', 'fetch',),
		'const struct sess *'
	),
	('bereq.http.',
		'HDR_BEREQ',
		( 'pipe', 'pass', 'miss', 'fetch',),
		( 'pipe', 'pass', 'miss', 'fetch',),
		'const struct sess *'
	),
	('bereq.connect_timeout',
		'DURATION',
		( 'pipe', 'pass', 'miss',),
		( 'pipe', 'pass', 'miss',),
		'struct sess *'
	),
	('bereq.first_byte_timeout',
		'DURATION',
		( 'pass', 'miss',),
		( 'pass', 'miss',),
		'struct sess *'
	),
	('bereq.between_bytes_timeout',
		'DURATION',
		( 'pass', 'miss',),
		( 'pass', 'miss',),
		'struct sess *'
	),
	('beresp.proto',
		'STRING',
		( 'fetch',),
		( 'fetch',),
		'const struct sess *'
	),
	('beresp.saintmode',
		'DURATION',
		( ),
		( 'fetch',),
		'const struct sess *'
	),
	('beresp.status',
		'INT',
		( 'fetch',),
		( 'fetch',),
		'const struct sess *'
	),
	('beresp.response',
		'STRING',
		( 'fetch',),
		( 'fetch',),
		'const struct sess *'
	),
	('beresp.http.',
		'HDR_BERESP',
		( 'fetch',),
		( 'fetch',),
		'const struct sess *'
	),
	('beresp.cacheable',
		'BOOL',
		( 'fetch',),
		( 'fetch',),
		'const struct sess *'
	),
	('beresp.do_esi',
		'BOOL',
		( 'fetch',),
		( 'fetch',),
		'const struct sess *'
	),
	('beresp.do_gzip',
		'BOOL',
		( 'fetch',),
		( 'fetch',),
		'const struct sess *'
	),
	('beresp.do_gunzip',
		'BOOL',
		( 'fetch',),
		( 'fetch',),
		'const struct sess *'
	),
	('beresp.ttl',
		'DURATION',
		( 'fetch',),
		( 'fetch',),
		'const struct sess *'
	),
	('beresp.grace',
		'DURATION',
		( 'fetch',),
		( 'fetch',),
		'struct sess *'
	),
	('beresp.backend.name',
		'STRING',
		( 'fetch',),
		( ),
		'const struct sess *'
	),
	('beresp.backend.ip',
		'IP',
		( 'fetch',),
		( ),
		'const struct sess *'
	),
	('beresp.backend.port',
		'INT',
		( 'fetch',),
		( ),
		'const struct sess *'
	),
	('beresp.storage',
		'STRING',
		( 'fetch',),
		( 'fetch',),
		'struct sess *'
	),
	('obj.proto',
		'STRING',
		( 'hit', 'error',),
		( 'hit', 'error',),
		'const struct sess *'
	),
	('obj.status',
		'INT',
		( 'error',),
		( 'error',),
		'const struct sess *'
	),
	('obj.response',
		'STRING',
		( 'error',),
		( 'error',),
		'const struct sess *'
	),
	('obj.hits',
		'INT',
		( 'hit', 'deliver',),
		( ),
		'const struct sess *'
	),
	('obj.http.',
		'HDR_OBJ',
		( 'hit', 'error',),
		( 'error',),		# XXX ?
		'const struct sess *'
	),
	('obj.ttl',
		'DURATION',
		( 'hit', 'error',),
		( 'hit', 'error',),
		'const struct sess *'
	),
	('obj.grace',
		'DURATION',
		( 'hit', 'error',),
		( 'hit', 'error',),
		'struct sess *'
	),
	('obj.lastuse',
		'DURATION',
		( 'hit', 'deliver', 'error',),
		( ),
		'const struct sess *'
	),
	('resp.proto',
		'STRING',
		( 'deliver',),
		( 'deliver',),
		'const struct sess *'
	),
	('resp.status',
		'INT',
		( 'deliver',),
		( 'deliver',),
		'const struct sess *'
	),
	('resp.response',
		'STRING',
		( 'deliver',),
		( 'deliver',),
		'const struct sess *'
	),
	('resp.http.',
		'HDR_RESP',
		( 'deliver',),
		( 'deliver',),
		'const struct sess *'
	),
	('now',
		'TIME',
		( 'all',),
		( ),
		'const struct sess *'
	),
)

stv_variables = (
	('free_space',	'BYTES',	"0."),
	('used_space',	'BYTES',	"0."),
	('happy',	'BOOL',		"0"),
)

#######################################################################
# VCL to C type conversion

vcltypes = {
	'IP':		"struct sockaddr_storage *",
	'STRING':	"const char *",
	'BOOL':		"unsigned",
	'BACKEND':	"struct director *",
	'TIME':		"double",
	'DURATION':	"double",
	'BYTES':	"double",
	'INT':		"int",
	'HEADER':	"const char *",
}

#######################################################################
# Nothing is easily configurable below this line.
#######################################################################

import sys
import copy

#######################################################################
# Emit a function to recognize tokens in a string

def emit_vcl_fixed_token(fo, tokens):

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
#define M1()\tdo {*q = p + 1; return (p[0]); } while (0)
#define M2(c,t)\tdo {if (p[1] == (c)) { *q = p + 2; return (t); }} while (0)

unsigned
vcl_fixed_token(const char *p, const char **q)
{

\tswitch (p[0]) {
""")
	last_initial = None
	for i in recog:
		if (i[0] == last_initial):
			continue
		last_initial = i[0]
		fo.write("\tcase '%s':\n" % last_initial)
		need_ret = True
		for j in rrecog:
			if (j[0] != last_initial):
				continue
			if len(j) == 2:
				fo.write("\t\tM2('%s', %s);\n" %
				    (j[1], emit[j]))
			elif len(j) == 1:
				fo.write("\t\tM1();\n")
				need_ret = False
			else:
				fo.write("\t\tif (")
				k = 1
				l = len(j)
				while (k < l):
					fo.write("p[%d] == '%s'" % (k, j[k]))
					fo.write(" &&")
					if (k % 3) == 0:
						fo.write("\n\t\t    ")
					else:
						fo.write(" ")
					k += 1
				fo.write("!isvar(p[%d])) {\n" % l)
				fo.write("\t\t\t*q = p + %d;\n" % l)
				fo.write("\t\t\treturn (%s);\n" % emit[j])
				fo.write("\t\t}\n")
		if need_ret:
			fo.write("\t\treturn (0);\n")
	fo.write("\tdefault:\n\t\treturn (0);\n\t}\n}\n")

#######################################################################
# Emit the vcl_tnames (token->string) conversion array

def emit_vcl_tnames(fo, tokens):
	fo.write("\nconst char * const vcl_tnames[256] = {\n")
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
# Read a C-source file and spit out code that outputs it with vsb_cat()

def emit_file(fo, fn):
	fi = open(fn)
	fc = fi.read()
	fi.close()

	w = 66		# Width of lines, after white space prefix
	maxlen = 10240	# Max length of string literal

	x = 0 
	l = 0
	fo.write("\n\t/* %s */\n\n" % fn)
	for c in fc:
		if l == 0:
			fo.write("\tvsb_cat(sb, \"")
			l += 12
			x += 12
		if x == 0:
			fo.write("\t    \"")
		d = c
		if c == '\n':
			d = "\\n"
		elif c == '\t':
			d = "\\t"
		elif c == '"':
			d = "\\\""
		elif c == '\\':
			d = "\\\\"

		if c == '\n' and x > w - 20:
			fo.write(d + "\"\n")
			x = 0
			continue
		if c.isspace() and x > w - 10:
			fo.write(d + "\"\n")
			x = 0
			continue
		
		fo.write(d)
		x += len(d)
		l += len(d)
		if l > maxlen:
			fo.write("\");\n")
			l = 0;
			x = 0
		if x > w - 3:
			fo.write("\"\n")
			x = 0
	if x != 0:
		fo.write("\"")
	if l != 0:
		fo.write("\t);\n")

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

fo = open(buildroot + "/lib/libvcl/vcc_token_defs.h", "w")

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

rets = dict()
vcls = list()
for i in returns:
	vcls.append(i[0])
	for j in i[1]:
		rets[j] = True
	
#######################################################################

fo = open(buildroot + "/include/vcl_returns.h", "w")

file_header(fo)

fo.write("\n/*lint -save -e525 -e539 */\n")

fo.write("\n#ifdef VCL_RET_MAC\n")
l = list(rets.keys())
l.sort()
for i in l:
	fo.write("VCL_RET_MAC(%s, %s" % (i.lower(), i.upper()))
	s=", "
	for j in returns:
		if i in j[1]:
			fo.write("%sVCL_MET_%s" % (s, j[0].upper()))
			s = " | "
	fo.write(")\n")
fo.write("#endif\n")

fo.write("\n#ifdef VCL_MET_MAC\n")
for i in returns:
	fo.write("VCL_MET_MAC(%s,%s,\n" % (i[0].lower(), i[0].upper()))
	p = " ("
	for j in i[1]:
		fo.write("    %s(1U << VCL_RET_%s)\n" % (p, j.upper()))
		p = "| "
	fo.write("))\n")
fo.write("#endif\n")
fo.write("\n/*lint -restore */\n")
fo.close()

#######################################################################

fo = open(buildroot + "/include/vcl.h", "w")

file_header(fo)

fo.write("""
struct sess;
struct cli;

typedef void vcl_init_f(struct cli *);
typedef void vcl_fini_f(struct cli *);
typedef int vcl_func_f(struct sess *sp);
""")


fo.write("\n/* VCL Methods */\n")
n = 0
for i in returns:
	fo.write("#define VCL_MET_%s\t\t(1U << %d)\n" % (i[0].upper(), n))
	n += 1

fo.write("\n#define VCL_MET_MAX\t\t%d\n" % n)
fo.write("\n#define VCL_MET_MASK\t\t0x%x\n" % ((1 << n) - 1))


fo.write("\n/* VCL Returns */\n")
n = 0
l = list(rets.keys())
l.sort()
for i in l:
	fo.write("#define VCL_RET_%s\t\t%d\n" % (i.upper(), n))
	n += 1

fo.write("\n#define VCL_RET_MAX\t\t%d\n" % n)


fo.write("""
struct VCL_conf {
	unsigned	magic;
#define VCL_CONF_MAGIC	0x7406c509	/* from /dev/random */

	struct director	**director;
	unsigned	ndirector;
	struct vrt_ref	*ref;
	unsigned	nref;
	unsigned	busy;
	unsigned	discard;

	unsigned	nsrc;
	const char	**srcname;
	const char	**srcbody;

	vcl_init_f	*init_func;
	vcl_fini_f	*fini_func;
""")

for i in returns:
	fo.write("\tvcl_func_f\t*" + i[0] + "_func;\n")

fo.write("""
};
""")

fo.close()

#######################################################################

def restrict(fo, spec):
	if len(spec) == 0:
		fo.write("\t    0,\n")
		return
	if spec[0] == 'all':
		spec = vcls
	p = ""
	n = 0
	for j in spec:
		if n == 4:
			fo.write("\n")
			n = 0
		if n == 0:
			fo.write("\t    ")
		n += 1
		fo.write(p + "VCL_MET_" + j.upper())
		p = " | "
		
	fo.write(",\n")

#######################################################################

fh = open(buildroot + "/include/vrt_obj.h", "w")
file_header(fh)

fo = open(buildroot + "/lib/libvcl/vcc_obj.c", "w")
file_header(fo)

fo.write("""
#include "config.h"
#include <stdio.h>
#include "vcc_compile.h"

const struct var vcc_vars[] = {
""")

for i in sp_variables:
	typ = i[1]
	if typ[:4] == "HDR_":
		typ = "HEADER"

	cnam = i[0].replace(".", "_")
	ctyp = vcltypes[typ]

	fo.write("\t{ \"%s\", %s, %d,\n" % (i[0], typ, len(i[0])))

	if len(i[2]) > 0:
		fo.write('\t    "VRT_r_%s(sp)",\n' % cnam)
		if typ != "HEADER":
			fh.write(ctyp + " VRT_r_%s(%s);\n" % (cnam, i[4]))
	else:
		fo.write('\t    NULL,\t/* No reads allowed */\n')
	restrict(fo, i[2])

	if len(i[3]) > 0:
		fo.write('\t    "VRT_l_%s(sp, ",\n' % cnam)
		if typ != "HEADER":
			fh.write("void VRT_l_%s(%s, " % (cnam, i[4]))
			if typ != "STRING":
				fh.write(ctyp + ");\n")
			else:
				fh.write(ctyp + ", ...);\n")
	else:
		fo.write('\t    NULL,\t/* No writes allowed */\n')
	restrict(fo, i[3])

	if typ == "HEADER":
		fo.write('\t    "%s",\n' % i[1])
	else:
		fo.write('\t    0,\n')		# XXX: shoule be NULL
	fo.write("\t},\n")

fo.write("\t{ NULL }\n};\n")

for i in stv_variables:
	fh.write(vcltypes[i[1]] + " VRT_Stv_" + i[0] + "(const char *);\n")

fo.close()
fh.close()

#######################################################################

fo = open(buildroot + "/lib/libvcl/vcc_fixed_token.c", "w")

file_header(fo)
fo.write("""

#include "config.h"
#include <stdio.h>
#include <ctype.h>
#include "config.h"
#include "vcc_priv.h"
#include "vsb.h"
""")

emit_vcl_fixed_token(fo, tokens)
emit_vcl_tnames(fo, tokens)

fo.write("""
void
vcl_output_lang_h(struct vsb *sb)
{
""")

emit_file(fo, buildroot + "/include/vcl.h")
emit_file(fo, srcroot + "/include/vrt.h")
emit_file(fo, buildroot + "/include/vrt_obj.h")

fo.write("""
}
""")

fo.close()

#######################################################################

fo = open(buildroot + "/include/vrt_stv_var.h", "w")

file_header(fo)

fo.write("""
#ifndef VRTSTVTYPE
#define VRTSTVTYPE(ct)
#define VRTSTVTYPEX
#endif
#ifndef VRTSTVVAR
#define VRTSTVVAR(nm, vtype, ctype, dval)
#define VRTSTVVARX
#endif
""")

x=dict()
for i in stv_variables:
	ct = vcltypes[i[1]]
	if not ct in x:
		fo.write("VRTSTVTYPE(" + ct + ")\n")
		x[ct] = 1
	fo.write("VRTSTVVAR(" + i[0] + ",\t" + i[1] + ",\t")
	fo.write(ct + ",\t" + i[2] + ")")
	fo.write("\n")

fo.write("""
#ifdef VRTSTVTYPEX
#undef VRTSTVTYPEX
#undef VRTSTVTYPE
#endif
#ifdef VRTSTVVARX
#undef VRTSTVVARX
#undef VRTSTVVAR
#endif
""")

fo.close
