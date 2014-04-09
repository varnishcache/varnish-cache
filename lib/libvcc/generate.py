#!/usr/bin/env python3
#-
# Copyright (c) 2006 Verdens Gang AS
# Copyright (c) 2006-2014 Varnish Software AS
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

	###############################################################
	# Client side

	('recv',
		"C",
		('synth', 'pass', 'pipe', 'hash', 'purge',)
	),
	('pipe',
		"C",
		('synth', 'pipe',)
	),
	('pass',
		"C",
		('synth', 'restart', 'fetch',)
	),
	('hash',
		"C",
		('lookup',)
	),
	('purge',
		"C",
		('synth', 'fetch',)
	),
	('miss',
		"C",
		('synth', 'restart', 'pass', 'fetch',)
	),
	('hit',
		"C",
		('synth', 'restart', 'pass', 'fetch', 'deliver',)
	),
	('deliver',
		"C",
		('restart', 'deliver',)
	),
	('synth',
		"C",
		('restart', 'deliver',)
	),

	###############################################################
	# Backend-fetch

	('backend_fetch',
		"B",
		('fetch', 'abandon')
	),
	('backend_response',
		"B",
		('deliver', 'retry', 'abandon')
	),
	('backend_error',
		"B",
		('deliver', 'retry')
	),

	###############################################################
	# Housekeeping

	('init',
		"",
		('ok',)
	),
	('fini',
		"",
		('ok',)
	),
)

#######################################################################
# Variables available in sessions
#
# 'all' means all methods
# 'client' means all methods tagged "C"
# 'backend' means all methods tagged "B"
# 'both' means all methods tagged "B" or "C"

sp_variables = [
	('client.ip',
		'IP',
		( 'client',),
		( ), """
		The client's IP address.
		"""
	),
	('client.identity',
		'STRING',
		( 'client',),
		( 'client',), """
		Identification of the client, used to load balance
		in the client director.
		"""
	),
	('server.ip',
		'IP',
		( 'client',),
		( ), """
		The IP address of the socket on which the client
		connection was received.
		"""
	),
	('server.hostname',
		'STRING',
		( 'client',),
		( ), """
		The host name of the server.
		"""
	),
	('server.identity',
		'STRING',
		( 'client',),
		( ), """
		The identity of the server, as set by the -i
		parameter.  If the -i parameter is not passed to varnishd,
		server.identity will be set to the name of the instance, as
		specified by the -n parameter.
		"""
	),
	('req.method',
		'STRING',
		( 'client',),
		( 'client',), """
		The request type (e.g. "GET", "HEAD").
		"""
	),
	('req.url',
		'STRING',
		( 'client',),
		( 'client',), """
		The requested URL.
		"""
	),
	('req.proto',
		'STRING',
		( 'client',),
		( 'client',), """
		The HTTP protocol version used by the client.
		"""
	),
	('req.http.',
		'HEADER',
		( 'client',),
		( 'client',), """
		The corresponding HTTP header.
		"""
	),
	('req.restarts',
		'INT',
		( 'client',),
		( ), """
		A count of how many times this request has been restarted.
		"""
	),
	('req.esi_level',
		'INT',
		( 'client',),
		( ), """
		A count of how many levels of ESI requests we're currently at.
		"""
	),
	('req.ttl',
		'DURATION',
		( 'client',),
		( 'client',), """
		"""
	),
	('req.xid',
		'STRING',
		( 'client',),
		( ), """
		Unique ID of this request.
		"""
	),
	('req.esi',
		'BOOL',
		( 'recv', 'backend_response', 'deliver', 'synth',),
		( 'recv', 'backend_response', 'deliver', 'synth',), """
		Boolean. Set to false to disable ESI processing
		regardless of any value in beresp.do_esi. Defaults
		to true. This variable is subject to change in
		future versions, you should avoid using it.
		"""
	),
	('req.can_gzip',
		'BOOL',
		( 'client',),
		( ), """
		Does the client accept the gzip transfer encoding.
		"""
	),
	('req.backend_hint',
		'BACKEND',
		( 'client', ),
		( 'client',), """
		Set bereq.backend to this if we attempt to fetch.
		"""
	),
	('req.hash_ignore_busy',
		'BOOL',
		( 'recv',),
		( 'recv',), """
		Ignore any busy object during cache lookup. You
		would want to do this if you have two server looking
		up content from each other to avoid potential deadlocks.
		"""
	),
	('req.hash_always_miss',
		'BOOL',
		( 'recv',),
		( 'recv',), """
		Force a cache miss for this request. If set to true
		Varnish will disregard any existing objects and
		always (re)fetch from the backend.
		"""
	),
	('bereq.xid',
		'STRING',
		( 'backend',),
		( ), """
		Unique ID of this request.
		"""
	),
	('bereq.retries',
		'INT',
		( 'backend',),
		( ), """
		"""
	),
	('bereq.backend',
		'BACKEND',
		( 'pipe', 'backend', ),
		( 'pipe', 'backend', ), """
		"""
	),
	('bereq.method',
		'STRING',
		( 'pipe', 'backend', ),
		( 'pipe', 'backend', ), """
		The request type (e.g. "GET", "HEAD").
		"""
	),
	('bereq.url',
		'STRING',
		( 'pipe', 'backend', ),
		( 'pipe', 'backend', ), """
		The requested URL.
		"""
	),
	('bereq.proto',
		'STRING',
		( 'pipe', 'backend', ),
		( 'pipe', 'backend', ), """
		The HTTP protocol version used to talk to the server.
		"""
	),
	('bereq.http.',
		'HEADER',
		( 'pipe', 'backend', ),
		( 'pipe', 'backend', ), """
		The corresponding HTTP header.
		"""
	),
	('bereq.uncacheable',
		'BOOL',
		( 'backend', ),
		( 'backend', ), """
		"""
	),
	('bereq.connect_timeout',
		'DURATION',
		( 'pipe', 'backend', ),
		( 'pipe', 'backend', ), """
		The time in seconds to wait for a backend connection.
		"""
	),
	('bereq.first_byte_timeout',
		'DURATION',
		( 'backend', ),
		( 'backend', ), """
		The time in seconds to wait for the first byte from
		the backend.  Not available in pipe mode.
		"""
	),
	('bereq.between_bytes_timeout',
		'DURATION',
		( 'backend', ),
		( 'backend', ), """
		The time in seconds to wait between each received byte from the
		backend.  Not available in pipe mode.
		"""
	),
	('beresp.proto',
		'STRING',
		( 'backend_response', 'backend_error'),
		( 'backend_response', 'backend_error'), """
		The HTTP protocol version used the backend replied with.
		"""
	),
	('beresp.status',
		'INT',
		( 'backend_response', 'backend_error'),
		( 'backend_response', 'backend_error'), """
		The HTTP status code returned by the server.
		"""
	),
	('beresp.reason',
		'STRING',
		( 'backend_response', 'backend_error'),
		( 'backend_response', 'backend_error'), """
		The HTTP status message returned by the server.
		"""
	),
	('beresp.http.',
		'HEADER',
		( 'backend_response', 'backend_error'),
		( 'backend_response', 'backend_error'), """
		The corresponding HTTP header.
		"""
	),
	('beresp.do_esi',
		'BOOL',
		( 'backend_response', 'backend_error'),
		( 'backend_response', 'backend_error'), """
		Boolean. ESI-process the object after fetching it.
		Defaults to false. Set it to true to parse the
		object for ESI directives. Will only be honored if
		req.esi is true.
		"""
	),
	('beresp.do_stream',
		'BOOL',
		( 'backend_response', 'backend_error'),
		( 'backend_response', 'backend_error'), """
		Deliver the object to the client directly without
		fetching the whole object into varnish. If this
		request is pass'ed it will not be stored in memory.
		As of Varnish Cache 3.0 the object will marked as busy
		as it is delivered so only client can access the object.
		"""
	),
	('beresp.do_gzip',
		'BOOL',
		( 'backend_response', 'backend_error'),
		( 'backend_response', 'backend_error'), """
		Boolean. Gzip the object before storing it. Defaults
		to false. When http_gzip_support is on Varnish will
		request already compressed content from the backend
		and as such compression in Varnish is not needed.
		"""
	),
	('beresp.do_gunzip',
		'BOOL',
		( 'backend_response', 'backend_error'),
		( 'backend_response', 'backend_error'), """
		Boolean. Unzip the object before storing it in the
		cache.  Defaults to false.
		"""
	),
	('beresp.uncacheable',
		'BOOL',
		( 'backend_response', 'backend_error'),
		( 'backend_response', 'backend_error'), """
		"""
	),
	('beresp.ttl',
		'DURATION',
		( 'backend_response', 'backend_error'),
		( 'backend_response', 'backend_error'), """
		The object's remaining time to live, in seconds.
		beresp.ttl is writable.
		"""
	),
	('beresp.grace',
		'DURATION',
		( 'backend_response', 'backend_error'),
		( 'backend_response', 'backend_error'), """
		Set to a period to enable grace.
		"""
	),
	('beresp.keep',
		'DURATION',
		( 'backend_response', 'backend_error'),
		( 'backend_response', 'backend_error'), """
		"""
	),
	('beresp.backend.name',
		'STRING',
		( 'backend_response', 'backend_error'),
		( ), """
		Name of the backend this response was fetched from.
		"""
	),
	('beresp.backend.ip',
		'IP',
		( 'backend_response', 'backend_error'),
		( ), """
		IP of the backend this response was fetched from.
		"""
	),
	('beresp.storage_hint',
		'STRING',
		( 'backend_response', 'backend_error'),
		( 'backend_response', 'backend_error'), """
		Hint to Varnish that you want to save this object to a
		particular storage backend.
		"""
	),
	('obj.proto',
		'STRING',
		( 'hit', ),
		( ), """
		The HTTP protocol version used when the object was retrieved.
		"""
	),
	('obj.status',
		'INT',
		( 'hit',),
		( ), """
		The HTTP status code returned by the server.
		"""
	),
	('obj.reason',
		'STRING',
		( 'hit',),
		( ), """
		The HTTP status message returned by the server.
		"""
	),
	('obj.hits',
		'INT',
		( 'hit', 'deliver',),
		( ), """
		The approximate number of times the object has been
		delivered. A value of 0 indicates a cache miss.
		This variable is also available in vcl_deliver.
		"""
	),
	('obj.http.',
		'HEADER',
		( 'hit', ),
		( ), """
		The corresponding HTTP header.
		"""
	),
	('obj.ttl',
		'DURATION',
		( 'hit', ),
		( ), """
		The object's remaining time to live, in seconds.
		obj.ttl is writable.
		"""
	),
	('obj.grace',
		'DURATION',
		( 'hit', ),
		( ), """
		The object's grace period in seconds. obj.grace is writable.
		"""
	),
	('obj.keep',
		'DURATION',
		( 'hit', ),
		( ), """
		"""
	),
	('obj.uncacheable',
		'BOOL',
		( 'hit', ),
		( ), """
		"""
	),
	('resp.proto',
		'STRING',
		( 'deliver', 'synth', ),
		( 'deliver', 'synth', ), """
		The HTTP protocol version to use for the response.
		"""
	),
	('resp.status',
		'INT',
		( 'deliver', 'synth', ),
		( 'deliver', 'synth', ), """
		The HTTP status code that will be returned.
		"""
	),
	('resp.reason',
		'STRING',
		( 'deliver', 'synth', ),
		( 'deliver', 'synth', ), """
		The HTTP status message that will be returned.
		"""
	),
	('resp.http.',
		'HEADER',
		( 'deliver', 'synth', ),
		( 'deliver', 'synth', ), """
		The corresponding HTTP header.
		"""
	),
	('now',
		'TIME',
		( 'all',),
		( ), """
		The current time, in seconds since the epoch. When
		used in string context it returns a formatted string.
		"""
	),
]

# Backwards compatibility:
aliases = [
]

stv_variables = (
	('free_space',	'BYTES',	"0."),
	('used_space',	'BYTES',	"0."),
	('happy',	'BOOL',		"0"),
)

#######################################################################
# VCL to C type conversion

vcltypes = {
	'STRING_LIST':	"void*",
}

fi = open(srcroot + "/include/vrt.h")

for i in fi:
	j = i.split();
	if len(j) < 3:
		continue
	if j[0] != "typedef":
		continue
	if j[-1][-1] != ";":
		continue
	if j[-1][:4] != "VCL_":
		continue
	d = " ".join(j[1:-1])
	vcltypes[j[-1][4:-1]] = d
fi.close()

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
# Read a C-source file and spit out code that outputs it with VSB_cat()

def emit_file(fo, fd, bn):
	fn = fd + "/" + bn

	fi = open(fn)
	fc = fi.read()
	fi.close()

	w = 66		# Width of lines, after white space prefix
	maxlen = 10240	# Max length of string literal

	x = 0
	l = 0
	fo.write("\n\t/* %s */\n\n" % fn)
	fo.write('\tVSB_cat(sb, "/* ---===### %s ###===--- */\\n");\n' % bn)
	for c in fc:
		if l == 0:
			fo.write("\tVSB_cat(sb, \"")
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

fo = open(buildroot + "/lib/libvcc/vcc_token_defs.h", "w")

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
vcls_client = list()
vcls_backend = list()
for i in returns:
	vcls.append(i[0])
	for j in i[1]:
		if j == "B":
			vcls_backend.append(i[0])
		elif j == "C":
			vcls_client.append(i[0])
	for j in i[2]:
		rets[j] = True

#######################################################################

fo = open(buildroot + "/include/tbl/vcl_returns.h", "w")

file_header(fo)

fo.write("\n/*lint -save -e525 -e539 */\n")

fo.write("\n#ifdef VCL_RET_MAC\n")
l = list(rets.keys())
l.sort()
for i in l:
	fo.write("VCL_RET_MAC(%s, %s" % (i.lower(), i.upper()))
	s=", "
	for j in returns:
		if i in j[2]:
			fo.write("%sVCL_MET_%s" % (s, j[0].upper()))
			s = " | "
	fo.write(")\n")
fo.write("#endif\n")

fo.write("\n#ifdef VCL_MET_MAC\n")
for i in returns:
	fo.write("VCL_MET_MAC(%s,%s,\n" % (i[0].lower(), i[0].upper()))
	p = " ("
	for j in i[2]:
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
struct vrt_ctx;
struct req;
struct busyobj;
struct ws;
struct cli;
struct worker;

typedef int vcl_init_f(struct cli *);
typedef void vcl_fini_f(struct cli *);
typedef int vcl_func_f(const struct vrt_ctx *ctx);
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

	vcl_init_f	*init_vcl;
	vcl_fini_f	*fini_vcl;
""")

for i in returns:
	fo.write("\tvcl_func_f\t*" + i[0] + "_func;\n")

fo.write("""
};
""")

fo.close()

#######################################################################

def restrict(fo, spec):
	d = dict()
	for j in spec:
		if j == 'all':
			for i in vcls:
				d[i] = True
		elif j == 'backend':
			for i in vcls_backend:
				d[i] = True
		elif j == 'client':
			for i in vcls_client:
				d[i] = True
		elif j == 'both':
			for i in vcls_client:
				d[i] = True
			for i in vcls_backend:
				d[i] = True
		else:
			assert j in vcls
			d[j] = True
	p = ""
	n = 0
	l = list(d.keys())
	l.sort()
	w = 0
	fo.write("\t\t")
	for j in l:
		x = p + "VCL_MET_" + j.upper()
		if w + len(x) > 60:
			fo.write("\n\t\t")
			w = 0
		fo.write(x)
		w += len(x)
		p = " | "
	if len(d) == 0:
		fo.write("0")
	fo.write(",\n")

#######################################################################

fh = open(buildroot + "/include/vrt_obj.h", "w")
file_header(fh)

fo = open(buildroot + "/lib/libvcc/vcc_obj.c", "w")
file_header(fo)

fo.write("""
#include "config.h"

#include <stdio.h>

#include "vcc_compile.h"

const struct var vcc_vars[] = {
""")

def one_var(nm, spec):
	fh.write("\n")
	typ = spec[1]
	cnam = i[0].replace(".", "_")
	ctyp = vcltypes[typ]

	fo.write("\t{ \"%s\", %s, %d,\n" % (nm, typ, len(nm)))

	if len(spec[2]) == 0:
		fo.write('\t    NULL,\t/* No reads allowed */\n')
	elif typ == "HEADER":
		fo.write('\t    "HDR_')
		fo.write(nm.split(".")[0].upper())
		fo.write('",\n')
	else:
		fo.write('\t    "VRT_r_%s(ctx)",\n' % cnam)
		if nm == i[0]:
			fh.write("VCL_" + typ +
			    " VRT_r_%s(const struct vrt_ctx *);\n" % cnam )
	restrict(fo, spec[2])

	if len(spec[3]) == 0:
		fo.write('\t    NULL,\t/* No writes allowed */\n')
	elif typ == "HEADER":
		fo.write('\t    "HDR_')
		fo.write(nm.split(".")[0].upper())
		fo.write('",\n')
	else:
		fo.write('\t    "VRT_l_%s(ctx, ",\n' % cnam)
		if nm == i[0]:
			fh.write(
			    "void VRT_l_%s(const struct vrt_ctx *, " % cnam)
			if typ != "STRING":
				fh.write("VCL_" + typ + ");\n")
			else:
				fh.write(ctyp + ", ...);\n")
	restrict(fo, spec[3])

	fo.write("\t},\n")


sp_variables.sort()
aliases.sort()
for i in sp_variables:
	one_var(i[0], i)
	for j in aliases:
		if j[1] == i[0]:
			one_var(j[0], i)

fo.write("\t{ NULL }\n};\n")

fh.write("\n")
for i in stv_variables:
	fh.write(vcltypes[i[1]] + " VRT_Stv_" + i[0] + "(const char *);\n")

fo.close()
fh.close()

#######################################################################

fo = open(buildroot + "/lib/libvcc/vcc_fixed_token.c", "w")

file_header(fo)
fo.write("""

#include "config.h"

#include <ctype.h>
#include <stdio.h>

#include "vcc_compile.h"
""")

emit_vcl_fixed_token(fo, tokens)
emit_vcl_tnames(fo, tokens)

fo.write("""
void
vcl_output_lang_h(struct vsb *sb)
{
""")

emit_file(fo, buildroot, "include/vcl.h")
emit_file(fo, srcroot, "include/vrt.h")
emit_file(fo, buildroot, "include/vrt_obj.h")

fo.write("""
}
""")

fo.close()

#######################################################################
ft = open(buildroot + "/include/tbl/vcc_types.h", "w")
file_header(ft)

ft.write("/*lint -save -e525 -e539 */\n")

i = list(vcltypes.keys())
i.sort()
for j in i:
	ft.write("VCC_TYPE(" + j + ")\n")
ft.write("/*lint -restore */\n")
ft.close()

#######################################################################

fo = open(buildroot + "/include/tbl/vrt_stv_var.h", "w")

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

#######################################################################

fp_vclvar = open(buildroot + "/doc/sphinx/include/vcl_var.rst", "w")

l = list()
for i in sp_variables:
	l.append(i)

l.sort()

def rst_where(fo, h, l):
	ll = list()
	if len(l) == 0:
		return
	fo.write("\t" + h)
	s = ""
	for j in l:
		if j == "both":
			ll.append("client")
			ll.append("backend")
		elif j == "client":
			ll.append(j)
		elif j == "backend":
			ll.append(j)
		else:
			ll.append("vcl_" + j)
	for j in ll:
		fo.write(s + j)
		s = ", "
	fo.write("\n\n")

hdr=""
for i in l:
	j = i[0].split(".")
	if j[0] != hdr:
		fp_vclvar.write("\n" + j[0] + "\n")
		fp_vclvar.write("~" * len(j[0]) + "\n")
		hdr = j[0]
	fp_vclvar.write("\n" + i[0] + "\n\n")
	fp_vclvar.write("\tType: " + i[1] + "\n\n")
	rst_where(fp_vclvar, "Readable from: ", i[2])
	rst_where(fp_vclvar, "Writable from: ", i[3])
	for j in i[4].split("\n"):
		fp_vclvar.write("\t%s\n" % j.strip())

fp_vclvar.close()
