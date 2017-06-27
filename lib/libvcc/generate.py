#!/usr/bin/env python
#-
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
# Generate various .c and .h files for the VCL compiler and the interfaces
# for it.

from __future__ import print_function

import subprocess
import os

#######################################################################
# These are our tokens

# We could drop all words such as "include", "if" etc, and use the
# ID type instead, but declaring them tokens makes them reserved words
# which hopefully makes for better error messages.
# XXX: does it actually do that ?

import copy
import sys
from os.path import join

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

returns = (
	###############################################################
	# Client side

	('recv',
		"C",
		('fail', 'synth', 'pass', 'pipe', 'hash', 'purge', 'vcl')
	),
	('pipe',
		"C",
		('fail', 'synth', 'pipe',)
	),
	('pass',
		"C",
		('fail', 'synth', 'restart', 'fetch',)
	),
	('hash',
		"C",
		('fail', 'lookup',)
	),
	('purge',
		"C",
		('fail', 'synth', 'restart',)
	),
	('miss',
		"C",
		('fail', 'synth', 'restart', 'pass', 'fetch',)
	),
	('hit',
		"C",
		('fail', 'synth', 'restart', 'pass', 'miss', 'deliver',)
	),
	('deliver',
		"C",
		('fail', 'synth', 'restart', 'deliver',)
	),
	('synth',
		"C",
		('fail', 'restart', 'deliver',)
	),

	###############################################################
	# Backend-fetch

	('backend_fetch',
		"B",
		('fail', 'fetch', 'abandon')
	),
	('backend_response',
		"B",
		('fail', 'deliver', 'retry', 'abandon', 'pass')
	),
	('backend_error',
		"B",
		('fail', 'deliver', 'retry', 'abandon')
	),

	###############################################################
	# Housekeeping

	('init',
		"H",
		('ok', 'fail')
	),
	('fini',
		"H",
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
	('remote.ip',
		'IP',
		('both',),
		(), """
		The IP address of the other end of the TCP connection.
		This can either be the clients IP, or the outgoing IP
		of a proxy server.
		"""
	),
	('client.ip',
		'IP',
		('both',),
		(), """
		The client's IP address.
		"""
	),
	('client.identity',
		'STRING',
		('client',),
		('client',), """
		Identification of the client, used to load balance
		in the client director. Defaults to the client's IP
		address.
		"""
	),
	('local.ip',
		'IP',
		('both',),
		(), """
		The IP address of the local end of the TCP connection.
		"""
	),
	('server.ip',
		'IP',
		('both',),
		(), """
		The IP address of the socket on which the client
		connection was received.
		"""
	),
	('server.hostname',
		'STRING',
		('all',),
		(), """
		The host name of the server.
		"""
	),
	('server.identity',
		'STRING',
		('all',),
		(), """
		The identity of the server, as set by the -i
		parameter.  If the -i parameter is not passed to varnishd,
		server.identity will be set to the hostname of the machine.
		"""
	),
	('req',
		'HTTP',
		('client',),
		(), """
		The entire request HTTP data structure
		"""
	),
	('req.method',
		'STRING',
		('client',),
		('client',), """
		The request type (e.g. "GET", "HEAD").
		"""
	),
	('req.url',
		'STRING',
		('client',),
		('client',), """
		The requested URL.
		"""
	),
	('req.proto',
		'STRING',
		('client',),
		('client',), """
		The HTTP protocol version used by the client.
		"""
	),
	('req.http.',
		'HEADER',
		('client',),
		('client',), """
		The corresponding HTTP header.
		"""
	),
	('req.restarts',
		'INT',
		('client',),
		(), """
		A count of how many times this request has been restarted.
		"""
	),
	('req.storage',
		'STEVEDORE',
		('recv',),
		('recv',), """
		The storage backend to use to save this request body.
		"""
	),
	('req.esi_level',
		'INT',
		('client',),
		(), """
		A count of how many levels of ESI requests we're currently at.
		"""
	),
	('req.ttl',
		'DURATION',
		('client',),
		('client',), """
		Upper limit on the object age for cache lookups to return hit.

		Usage of req.ttl should be replaced with a check on
		obj.ttl in vcl_hit, returning miss when needed, but
		this currently hits bug #1799, so an additional
		workaround is required.

		Deprecated and scheduled for removal with varnish release 7.
		"""
	),
	('req.xid',
		'STRING',
		('client',),
		(), """
		Unique ID of this request.
		"""
	),
	('req.esi',
		'BOOL',
		('client',),
		('client',), """
		Boolean. Set to false to disable ESI processing
		regardless of any value in beresp.do_esi. Defaults
		to true. This variable is subject to change in
		future versions, you should avoid using it.
		"""
	),
	('req.can_gzip',
		'BOOL',
		('client',),
		(), """
		Does the client accept the gzip transfer encoding.
		"""
	),
	('req.backend_hint',
		'BACKEND',
		('client', ),
		('client',), """
		Set bereq.backend to this if we attempt to fetch.
		When set to a director, reading this variable returns
		an actual backend if the director has resolved immediately,
		or the director otherwise.
		When used in string context, returns the name of the director
		or backend, respectively.
		"""
	),
	('req.hash_ignore_busy',
		'BOOL',
		('recv',),
		('recv',), """
		Ignore any busy object during cache lookup. You
		would want to do this if you have two server looking
		up content from each other to avoid potential deadlocks.
		"""
	),
	('req.hash_always_miss',
		'BOOL',
		('recv',),
		('recv',), """
		Force a cache miss for this request. If set to true
		Varnish will disregard any existing objects and
		always (re)fetch from the backend.
		"""
	),
	('req_top.method',
		'STRING',
		('client',),
		(), """
		The request method of the top-level request in a tree
		of ESI requests. (e.g. "GET", "HEAD").
		Identical to req.method in non-ESI requests.
		"""
	),
	('req_top.url',
		'STRING',
		('client',),
		(), """
		The requested URL of the top-level request in a tree
		of ESI requests.
		Identical to req.url in non-ESI requests.
		"""
	),
	('req_top.http.',
		'HEADER',
		('client',),
		(), """
		HTTP headers of the top-level request in a tree of ESI requests.
		Identical to req.http. in non-ESI requests.
		"""
	),
	('req_top.proto',
		'STRING',
		('client',),
		(), """
		HTTP protocol version of the top-level request in a tree of
		ESI requests.
		Identical to req.proto in non-ESI requests.
		"""
	),
	('bereq',
		'HTTP',
		('backend',),
		(), """
		The entire backend request HTTP data structure
		"""
	),
	('bereq.xid',
		'STRING',
		('backend',),
		(), """
		Unique ID of this request.
		"""
	),
	('bereq.retries',
		'INT',
		('backend',),
		(), """
		A count of how many times this request has been retried.
		"""
	),
	('bereq.backend',
		'BACKEND',
		('pipe', 'backend', ),
		('pipe', 'backend', ), """
		This is the backend or director we attempt to fetch from.
		When set to a director, reading this variable returns
		an actual backend if the director has resolved immediately,
		or the director otherwise.
		When used in string context, returns the name of the director
		or backend, respectively.
		"""
	),
	('bereq.body',
		'BODY',
		(),
		('backend_fetch',), """
		The request body.
		"""
	),
	('bereq.method',
		'STRING',
		('pipe', 'backend', ),
		('pipe', 'backend', ), """
		The request type (e.g. "GET", "HEAD").
		"""
	),
	('bereq.url',
		'STRING',
		('pipe', 'backend', ),
		('pipe', 'backend', ), """
		The requested URL.
		"""
	),
	('bereq.proto',
		'STRING',
		('pipe', 'backend', ),
		('pipe', 'backend', ), """
		The HTTP protocol version used to talk to the server.
		"""
	),
	('bereq.http.',
		'HEADER',
		('pipe', 'backend', ),
		('pipe', 'backend', ), """
		The corresponding HTTP header.
		"""
	),
	('bereq.uncacheable',
		'BOOL',
		('backend', ),
		(), """
		Indicates whether this request is uncacheable due
		to a pass in the client side or a hit on an hit-for-pass
		object.
		"""
	),
	('bereq.connect_timeout',
		'DURATION',
		('pipe', 'backend', ),
		('pipe', 'backend', ), """
		The time in seconds to wait for a backend connection.
		"""
	),
	('bereq.first_byte_timeout',
		'DURATION',
		('backend', ),
		('backend', ), """
		The time in seconds to wait for the first byte from
		the backend.  Not available in pipe mode.
		"""
	),
	('bereq.between_bytes_timeout',
		'DURATION',
		('backend', ),
		('backend', ), """
		The time in seconds to wait between each received byte from the
		backend.  Not available in pipe mode.
		"""
	),
	('beresp',
		'HTTP',
		('backend_response', 'backend_error'),
		(), """
		The entire backend response HTTP data structure
		"""
	),
	('beresp.body',
		'BODY',
		(),
		('backend_error',), """
		The response body.
		"""
	),
	('beresp.proto',
		'STRING',
		('backend_response', 'backend_error'),
		('backend_response', 'backend_error'), """
		The HTTP protocol version used the backend replied with.
		"""
	),
	('beresp.status',
		'INT',
		('backend_response', 'backend_error'),
		('backend_response', 'backend_error'), """
		The HTTP status code returned by the server.

		Status codes >1000 can be set for vcl-internal
		purposes and will be taken modulo 1000 on delivery.
		"""
	),
	('beresp.reason',
		'STRING',
		('backend_response', 'backend_error'),
		('backend_response', 'backend_error'), """
		The HTTP status message returned by the server.
		"""
	),
	('beresp.http.',
		'HEADER',
		('backend_response', 'backend_error'),
		('backend_response', 'backend_error'), """
		The corresponding HTTP header.
		"""
	),
	('beresp.do_esi',
		'BOOL',
		('backend_response', 'backend_error'),
		('backend_response', 'backend_error'), """
		Boolean. ESI-process the object after fetching it.
		Defaults to false. Set it to true to parse the
		object for ESI directives. Will only be honored if
		req.esi is true.
		"""
	),
	('beresp.do_stream',
		'BOOL',
		('backend_response', 'backend_error'),
		('backend_response', 'backend_error'), """
		Deliver the object to the client while fetching the whole
		object into varnish. For uncacheable objects, storage for
		parts of the body which have been sent to the client may
		get freed early, depending on the storage engine used.
		"""
	),
	('beresp.do_gzip',
		'BOOL',
		('backend_response', 'backend_error'),
		('backend_response', 'backend_error'), """
		Boolean. Gzip the object before storing it. Defaults
		to false. When http_gzip_support is on Varnish will
		request already compressed content from the backend
		and as such compression in Varnish is not needed.
		"""
	),
	('beresp.do_gunzip',
		'BOOL',
		('backend_response', 'backend_error'),
		('backend_response', 'backend_error'), """
		Boolean. Unzip the object before storing it in the
		cache.  Defaults to false.
		"""
	),
	('beresp.was_304',
		'BOOL',
		('backend_response', 'backend_error'),
		(), """
		Boolean. If this is a successful 304 response to a
		backend conditional request refreshing an existing
		cache object.
		"""
	),
	('beresp.uncacheable',
		'BOOL',
		('backend_response', 'backend_error'),
		('backend_response', 'backend_error'), """
		Inherited from bereq.uncacheable, see there.

		Setting this variable makes the object uncacheable, which may
		get stored as a hit-for-miss object in the cache.

		Clearing the variable has no effect and will log the warning
		"Ignoring attempt to reset beresp.uncacheable".
		"""
	),
	('beresp.ttl',
		'DURATION',
		('backend_response', 'backend_error'),
		('backend_response', 'backend_error'), """
		The object's remaining time to live, in seconds.
		"""
	),
	('beresp.age',
		'DURATION',
		('backend_response', 'backend_error'),
		(), """
		The age of the object.
		"""
	),
	('beresp.grace',
		'DURATION',
		('backend_response', 'backend_error'),
		('backend_response', 'backend_error'), """
		Set to a period to enable grace.
		"""
	),
	('beresp.keep',
		'DURATION',
		('backend_response', 'backend_error'),
		('backend_response', 'backend_error'), """
		Set to a period to enable conditional backend requests.

		The keep time is cache lifetime in addition to the ttl.

		Objects with ttl expired but with keep time left may be used
		to issue conditional (If-Modified-Since / If-None-Match)
		requests to the backend to refresh them.
		"""
	),
	('beresp.backend',
		'BACKEND',
		('backend_response', 'backend_error'),
		(), """
		This is the backend we fetched from.  If bereq.backend
		was set to a director, this will be the backend selected
		by the director.
		When used in string context, returns its name.
		"""
	),
	('beresp.backend.name',
		'STRING',
		('backend_response', 'backend_error'),
		(), """
		Name of the backend this response was fetched from.
		Same as beresp.backend.
		"""
	),
	('beresp.backend.ip',
		'IP',
		('backend_response',),
		(), """
		IP of the backend this response was fetched from.
		"""
	),
	('beresp.storage',
		'STEVEDORE',
		('backend_response', 'backend_error'),
		('backend_response', 'backend_error'), """
		The storage backend to use to save this object.
		"""
	),
	('beresp.storage_hint',
		'STRING',
		('backend_response', 'backend_error'),
		('backend_response', 'backend_error'), """
		Deprecated. Hint to Varnish that you want to
		save this object to a particular storage backend.
		Use beresp.storage instead.
		"""
	),
	('obj.proto',
		'STRING',
		('hit',),
		(), """
		The HTTP protocol version stored with the object.
		"""
	),
	('obj.status',
		'INT',
		('hit',),
		(), """
		The HTTP status code stored with the object.
		"""
	),
	('obj.reason',
		'STRING',
		('hit',),
		(), """
		The HTTP reason phrase stored with the object.
		"""
	),
	('obj.hits',
		'INT',
		('hit', 'deliver'),
		(), """
		The count of cache-hits on this object. A value of 0 indicates a
		cache miss.
		"""
	),
	('obj.http.',
		'HEADER',
		('hit',),
		(), """
		The corresponding HTTP header.
		"""
	),
	('obj.ttl',
		'DURATION',
		('hit', 'deliver'),
		(), """
		The object's remaining time to live, in seconds.
		"""
	),
	('obj.age',
		'DURATION',
		('hit', 'deliver'),
		(), """
		The age of the object.
		"""
	),
	('obj.grace',
		'DURATION',
		('hit', 'deliver'),
		(), """
		The object's remaining grace period in seconds.
		"""
	),
	('obj.keep',
		'DURATION',
		('hit', 'deliver'),
		(), """
		The object's remaining keep period in seconds.
		"""
	),
	('obj.uncacheable',
		'BOOL',
		('deliver',),
		(), """
		Whether the object is uncacheable (pass, hit-for-pass or
		hit-for-miss).
		"""
	),
	('resp',
		'HTTP',
		('deliver', 'synth'),
		(), """
		The entire response HTTP data structure.
		"""
	),
	('resp.body',
		'BODY',
		(),
		('synth',), """
		The response body.
		"""
	),
	('resp.proto',
		'STRING',
		('deliver', 'synth'),
		('deliver', 'synth'), """
		The HTTP protocol version to use for the response.
		"""
	),
	('resp.status',
		'INT',
		('deliver', 'synth'),
		('deliver', 'synth'), """
		The HTTP status code that will be returned.

		Assigning a HTTP standardized code to resp.status will also
		set resp.reason to the corresponding status message.

		resp.status 200 will get changed into 304 by core code after
		a return(deliver) from vcl_deliver for conditional requests
		to cached content if validation succeeds.
		"""
	),
	('resp.reason',
		'STRING',
		('deliver', 'synth'),
		('deliver', 'synth'), """
		The HTTP status message that will be returned.
		"""
	),
	('resp.http.',
		'HEADER',
		('deliver', 'synth'),
		('deliver', 'synth'), """
		The corresponding HTTP header.
		"""
	),
	('resp.is_streaming',
		'BOOL',
		('deliver', 'synth'),
		(), """
		Returns true when the response will be streamed
		from the backend.
		"""
	),
	('now',
		'TIME',
		('all',),
		(), """
		The current time, in seconds since the epoch. When
		used in string context it returns a formatted string.
		"""
	),
]

# Backwards compatibility:
aliases = []

stv_variables = (
	('free_space',	'BYTES',	"0.", 'storage.<name>.free_space', """
	Free space available in the named stevedore. Only available for
	the malloc stevedore.
	"""),
	('used_space',	'BYTES',	"0.", 'storage.<name>.used_space', """
	Used space in the named stevedore. Only available for the malloc
	stevedore.
	"""),
	('happy',	'BOOL',		"0", 'storage.<name>.happy', """
	Health status for the named stevedore. Not available in any of the
	current stevedores.
	"""),
)

#######################################################################
# VCL to C type conversion

vcltypes = {
	'STRING_LIST':	"void*",
}

fi = open(join(srcroot, "include/vrt.h"))

for i in fi:
	j = i.split()
	if len(j) < 3:
		continue
	if j[0] != "typedef":
		continue
	if j[-1][-1] != ";":
		continue
	if j[-1][-2] == ")":
		continue
	if j[-1][:4] != "VCL_":
		continue
	d = " ".join(j[1:-1])
	vcltypes[j[-1][4:-1]] = d
fi.close()

#######################################################################
# Nothing is easily configurable below this line.
#######################################################################


#######################################################################
def emit_vcl_fixed_token(fo, tokens):
	"Emit a function to recognize tokens in a string"
	recog = list()
	emit = dict()
	for i in tokens:
		j = tokens[i]
		if j is not None:
			recog.append(j)
			emit[j] = i

	recog.sort()
	rrecog = copy.copy(recog)
	rrecog.sort(key=lambda x: -len(x))

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
def emit_vcl_tnames(fo, tokens):
	"Emit the vcl_tnames (token->string) conversion array"
	fo.write("\nconst char * const vcl_tnames[256] = {\n")
	l = list(tokens.keys())
	l.sort()
	for i in l:
		j = tokens[i]
		if j is None:
			j = i
		if i[0] == "'":
			j = i
		fo.write("\t[%s] = \"%s\",\n" % (i, j))
	fo.write("};\n")


#######################################################################
def emit_file(fo, fd, bn):
	"Read a C-source file and spit out code that outputs it with VSB_cat()"
	fn = join(fd, bn)

	fi = open(fn)
	fc = fi.read()
	fi.close()

	w = 66		# Width of lines, after white space prefix
	maxlen = 10240  # Max length of string literal

	x = 0
	l = 0
	fo.write("\n\t/* %s */\n\n" % fn)
	fo.write('\tVSB_cat(sb, "/* ---===### %s ###===--- */\\n\\n");\n' % bn)
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
			l = 0
			x = 0
		if x > w - 3:
			fo.write("\"\n")
			x = 0
	if x != 0:
		fo.write("\"\n")
	if l != 0:
		fo.write("\t);\n")
	fo.write('\tVSB_cat(sb, "\\n");\n')

#######################################################################


def polish_tokens(tokens):
	"Expand single char tokens"
	st = tokens[None]
	del tokens[None]
	for i in st:
		tokens["'" + i + "'"] = i


#######################################################################
def file_header(fo):
	fo.write("""/*
 * NB:  This file is machine generated, DO NOT EDIT!
 *
 * Edit and run lib/libvcc/generate.py instead.
 */

""")

def lint_start(fo):
	fo.write('/*lint -save -e525 -e539 */\n\n')

def lint_end(fo):
	fo.write('\n/*lint -restore */\n')

#######################################################################

polish_tokens(tokens)

fo = open(join(buildroot, "lib/libvcc/vcc_token_defs.h"), "w")

file_header(fo)

j = 128
for i in sorted(tokens.keys()):
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

fo = open(join(buildroot, "include/tbl/vcl_returns.h"), "w")

file_header(fo)

lint_start(fo)

fo.write("#ifdef VCL_RET_MAC\n")
ll = sorted(returns)
for i in sorted(rets.keys()):
	fo.write("VCL_RET_MAC(%s, %s" % (i.lower(), i.upper()))
	s = ",\n\t"
	for j in ll:
		if i in j[2]:
			fo.write("%sVCL_MET_%s" % (s, j[0].upper()))
			s = " |\n\t"
	fo.write("\n)\n\n")
fo.write("#undef VCL_RET_MAC\n")
fo.write("#endif\n")

fo.write("\n#ifdef VCL_MET_MAC\n")
for i in ll:
	fo.write("VCL_MET_MAC(%s, %s, %s," %
	    (i[0].lower(), i[0].upper(), i[1]))
	p = " (\n\t"
	for j in sorted(i[2]):
		fo.write("%s(1U << VCL_RET_%s)" % (p, j.upper()))
		p = " |\n\t"
	fo.write(")\n)\n\n")
fo.write("#undef VCL_MET_MAC\n")
fo.write("#endif\n")
lint_end(fo)
fo.close()

#######################################################################

fo = open(join(buildroot, "include/vcl.h"), "w")

file_header(fo)

fo.write("""
struct vrt_ctx;
#define VRT_CTX const struct vrt_ctx *ctx
struct req;
struct busyobj;
struct ws;
struct cli;
struct worker;

enum vcl_event_e {
	VCL_EVENT_LOAD,
	VCL_EVENT_WARM,
	VCL_EVENT_COLD,
	VCL_EVENT_DISCARD,
};

typedef int vcl_event_f(VRT_CTX, enum vcl_event_e);
typedef int vcl_init_f(VRT_CTX);
typedef void vcl_fini_f(VRT_CTX);
typedef void vcl_func_f(VRT_CTX);
""")


def tbl40(a, b):
	while len(a.expandtabs()) < 40:
		a += "\t"
	return a + b

fo.write("\n/* VCL Methods */\n")
n = 1
for i in returns:
	fo.write(tbl40("#define VCL_MET_%s" % i[0].upper(), "(1U << %d)\n" % n))
	n += 1

fo.write("\n" + tbl40("#define VCL_MET_MAX", "%d\n" % n))
fo.write("\n" + tbl40("#define VCL_MET_MASK", "0x%x\n" % ((1 << n) - 1)))


fo.write("\n/* VCL Returns */\n")
n = 1
for i in sorted(rets.keys()):
	fo.write(tbl40("#define VCL_RET_%s" % i.upper(), "%d\n" % n))
	n += 1

fo.write("\n" + tbl40("#define VCL_RET_MAX", "%d\n" % n))


fo.write("""
struct VCL_conf {
	unsigned			magic;
#define VCL_CONF_MAGIC			0x7406c509	/* from /dev/random */

	struct director			**default_director;
	const struct vrt_backend_probe	*default_probe;
	unsigned			nref;
	struct vrt_ref			*ref;

	unsigned			nsrc;
	const char			**srcname;
	const char			**srcbody;

	vcl_event_f			*event_vcl;
""")

for i in returns:
	fo.write("\tvcl_func_f\t*" + i[0] + "_func;\n")

fo.write("\n};\n")
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

fh = open(join(buildroot, "include/vrt_obj.h"), "w")
file_header(fh)

fo = open(join(buildroot, "lib/libvcc/vcc_obj.c"), "w")
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

	fo.write("\t{ \"%s\", %s,\n" % (nm, typ))

	if len(spec[2]) == 0:
		fo.write('\t    NULL,\t/* No reads allowed */\n')
	elif typ == "HEADER":
		fo.write('\t    "HDR_')
		fo.write(nm.split(".")[0].upper())
		fo.write('",\n')
	else:
		fo.write('\t    "VRT_r_%s(ctx)",\n' % cnam)
		if nm == i[0]:
			fh.write("VCL_" + typ + " VRT_r_%s(VRT_CTX);\n" % cnam)
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
			fh.write("void VRT_l_%s(VRT_CTX, " % cnam)
			if typ != "STRING" and typ != "BODY":
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

fo.write("\t{ NULL }\n};\n\n")

for i in stv_variables:
	fh.write(vcltypes[i[1]] + " VRT_Stv_" + i[0] + "(const char *);\n")

fo.close()
fh.close()

#######################################################################

fo = open(join(buildroot, "lib/libvcc/vcc_fixed_token.c"), "w")

file_header(fo)
fo.write("""

#include "config.h"

#include "vcc_compile.h"
""")

emit_vcl_fixed_token(fo, tokens)
emit_vcl_tnames(fo, tokens)

fo.write("""
void
vcl_output_lang_h(struct vsb *sb)
{
""")

emit_file(fo, srcroot, "include/vdef.h")
emit_file(fo, buildroot, "include/vcl.h")
emit_file(fo, srcroot, "include/vrt.h")
emit_file(fo, buildroot, "include/vrt_obj.h")

fo.write("\n}\n")
fo.close()

#######################################################################
ft = open(join(buildroot, "include/tbl/vcc_types.h"), "w")
file_header(ft)

lint_start(ft)

for vcltype in sorted(vcltypes.keys()):
	ft.write("VCC_TYPE(" + vcltype + ")\n")
ft.write("#undef VCC_TYPE\n")
lint_end(ft)
ft.close()

#######################################################################

fo = open(join(buildroot, "include/tbl/vrt_stv_var.h"), "w")

file_header(fo)
lint_start(fo)

for i in stv_variables:
	ct = vcltypes[i[1]]
	fo.write("VRTSTVVAR(" + i[0] + ",\t" + i[1] + ",\t")
	fo.write(ct + ",\t" + i[2] + ")")
	fo.write("\n")

fo.write("#undef VRTSTVVAR\n")
lint_end(fo)
fo.close()

#######################################################################

fp_vclvar = open(join(buildroot, "doc/sphinx/include/vcl_var.rst"), "w")

l = sorted(sp_variables)


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
		elif j == "all":
			ll.append(j)
		else:
			ll.append("vcl_" + j)
	for j in ll:
		fo.write(s + j)
		s = ", "
	fo.write("\n\n")

hdr = ""
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

hdr = "storage"
fp_vclvar.write("\n" + hdr + "\n")
fp_vclvar.write("~" * len(hdr) + "\n")
for i in stv_variables:
	fp_vclvar.write("\n" + i[3] + "\n\n")
	fp_vclvar.write("\tType: " + i[1] + "\n\n")
	fp_vclvar.write("\tReadable from: client, backend\n\n")
	for j in i[4].split("\n"):
		fp_vclvar.write("\t%s\n" % j.strip())

fp_vclvar.close()

#######################################################################

if os.path.isdir(os.path.join(srcroot, ".git")):
	v = subprocess.check_output([
		"git --git-dir=" + os.path.join(srcroot, ".git") +
		" show -s --pretty=format:%h"
	], shell=True, universal_newlines=True)
	v = v.strip()
	b = subprocess.check_output([
		"git --git-dir=" + os.path.join(srcroot, ".git") +
		" rev-parse --abbrev-ref HEAD"
	], shell=True, universal_newlines=True)
	b = b.strip()
else:
	b = "NOGIT"
	v = "NOGIT"

vcsfn = os.path.join(srcroot, "include", "vcs_version.h")

try:
	i = open(vcsfn).readline()
except IOError:
	i = ""

if i != "/* " + v + " */":
	fo = open(vcsfn, "w")
	file_header(fo)
	fo.write('#define VCS_Version "%s"\n' % v)
	fo.write('#define VCS_Branch "%s"\n' % b)
	fo.close()

	for i in open(os.path.join(buildroot, "Makefile")):
		if i[:14] == "PACKAGE_STRING":
			break
	i = i.split("=")[1].strip()

	fo = open(os.path.join(srcroot, "include", "vmod_abi.h"), "w")
	file_header(fo)
	fo.write('#define VMOD_ABI_Version "%s %s"\n' % (i, v))
	fo.close()
