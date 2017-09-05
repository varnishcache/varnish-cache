#!/usr/bin/env python
# -*- encoding: utf-8 -*-
#
# Copyright (c) 2017 Varnish Software AS
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

"""
This program compiles a .vsc file to C language constructs.
"""

from __future__ import print_function

import getopt
import json
import sys
import gzip
import StringIO
import collections
import struct

TYPES = [ "counter", "gauge", "bitmap" ]
CTYPES = [ "uint64_t" ]
LEVELS = [ "info", "diag", "debug" ]
FORMATS = [ "integer", "bytes", "bitmap", "duration" ]

PARAMS = {
	"type":		["counter", TYPES],
	"ctype":	["uint64_t", CTYPES],
	"level":	["info", LEVELS],
	"oneliner":	True,
	"format":	[ "integer", FORMATS],
}

def gzip_str(s):
	out = StringIO.StringIO()
	gzip.GzipFile(fileobj=out, mode="w").write(s)
	out.seek(4)
	out.write(struct.pack("<L", 0x12bfd58))
	return out.getvalue()

def genhdr(fo, name):
	fo.write('/*\n')
	fo.write(' * NB:  This file is machine generated, DO NOT EDIT!\n')
	fo.write(' *\n')
	fo.write(' * Edit %s.vsc run lib/libvcc/vsctool.py instead.\n' % name)
	fo.write(' */\n')
	fo.write('\n')

#######################################################################

class vscset(object):
	def __init__(self, name, m):
		self.name = name
		self.struct = "struct VSC_" + name
		self.mbrs = []
		self.head = m
		self.completed = False
		self.off = 8

	def addmbr(self, m):
		assert not self.completed
		self.mbrs.append(m)
		m.param["index"] = self.off
		self.off += 8

	def complete(self):
		self.completed = True

	def emit_json(self, fo):
		dd = collections.OrderedDict()
		dd["version"] = "1"
		dd["name"] = self.name
		dd["oneliner"] = self.head.param["oneliner"].strip()
		dd["order"] = int(self.head.param["order"])
		dd["docs"] = "\n".join(self.head.getdoc())
		dd["elements"] = len(self.mbrs)
		el = collections.OrderedDict()
		dd["elem"] = el
		for i in self.mbrs:
			ed = collections.OrderedDict()
			el[i.arg] = ed
			for j in PARAMS:
				if j in i.param:
					ed[j] = i.param[j]
			ed["index"] = i.param["index"]
			ed["name"] = i.arg
			ed["docs"] = "\n".join(i.getdoc())
		s=json.dumps(dd, separators=(",",":")) + "\0"
		fo.write("\nstatic const size_t vsc_%s_jsonlen = %dL;\n" %
		    (self.name, len(s)))
		z = gzip_str(s)
		fo.write("\nstatic const unsigned char");
		fo.write(" vsc_%s_zjson[%d] = {\n" % (self.name, len(z)))
		bz = bytearray(z)
		t = "\t"
		for i in bz:
			t += "%d," % i
			if len(t) >= 70:
				fo.write(t + "\n")
				t = "\t"
		if len(t) > 1:
			fo.write(t[:-1])
		fo.write("\n};\n")
		s = json.dumps(dd, indent=2, separators=(',', ': '))
		fo.write("\n// ")
		fo.write("\n// ".join(s.split("\n")))
		fo.write("\n")


	def emit_h(self):
		fon="VSC_" + self.name + ".h"
		fo = open(fon, "w")
		genhdr(fo, self.name)
		fo.write(self.struct + " {\n")
		for i in self.mbrs:
			fo.write("\tuint64_t\t%s;\n" % i.arg)
		fo.write("};\n")
		fo.write("\n");

		fo.write(self.struct + " *VSC_" + self.name + "_New")
		fo.write("(const char *fmt, ...);\n");

		fo.write("void VSC_" + self.name + "_Destroy")
		fo.write("(" + self.struct + "**);\n")

		if 'sumfunction' in self.head.param:
			fo.write("void VSC_" + self.name + "_Summ")
			fo.write("(" + self.struct + " *, ")
			fo.write("const " + self.struct + " *);\n")

	def emit_c(self):
		fon="VSC_" + self.name + ".c"
		fo = open(fon, "w")
		genhdr(fo, self.name)
		fo.write('#include "config.h"\n')
		fo.write('#include <stddef.h>\n')
		fo.write('#include <stdarg.h>\n')
		fo.write('#include <stdio.h>\n')
		fo.write('#include <stdint.h>\n')
		fo.write('#include "vdef.h"\n')
		fo.write('#include "vas.h"\n')
		fo.write('#include "common/common_vsm.h"\n')
		fo.write('#include "VSC_%s.h"\n' % self.name)

		fo.write("\n")
		fo.write('static const char vsc_%s_name[] = "%s";\n' %
		    (self.name, self.name.upper()))

		fo.write("\n")
		for i in self.mbrs:
			fo.write("_Static_assert(offsetof(" + self.struct)
			fo.write(", " + i.arg + ")")
			fo.write(" == %d,\n" % (i.param["index"] - 8))
			fo.write('    "VSC element offset is wrong");\n')

		self.emit_json(fo)

		fo.write("\n")
		fo.write(self.struct + "*\n");
		fo.write("VSC_" + self.name + "_New")
		fo.write("(const char *fmt, ...)\n");
		fo.write("{\n")
		fo.write("\tva_list ap;\n")
		fo.write("\t" + self.struct + " *retval;\n")
		fo.write("\n")
		fo.write("\tva_start(ap, fmt);\n")
		fo.write("\tretval = VSC_Alloc")
		fo.write("(vsc_" + self.name + "_name, ")
		fo.write("sizeof(" + self.struct + "),\n\t    ")
		fo.write("vsc_" + self.name + "_jsonlen, ")
		fo.write("vsc_" + self.name + "_zjson, ")
		fo.write("sizeof vsc_" + self.name + "_zjson,\n")
		fo.write("\t    fmt, ap);\n")
		fo.write("\tva_end(ap);\n")
		fo.write("\treturn(retval);\n")
		fo.write("}\n")

		fo.write("\n")
		fo.write("void\n")
		fo.write("VSC_" + self.name + "_Destroy")
		fo.write("(" + self.struct + " **pp)\n")
		fo.write("{\n")
		fo.write("\n")
		fo.write("\tAN(pp);\n")
		fo.write('\tVSC_Destroy(vsc_%s_name, *pp);\n' % self.name)
		fo.write("\t*pp = NULL;\n")
		fo.write("}\n")

		if 'sumfunction' in self.head.param:
			fo.write("\n")
			fo.write("void\n")
			fo.write("VSC_" + self.name + "_Summ")
			fo.write("(" + self.struct + " *dst, ")
			fo.write("const " + self.struct + " *src)\n")
			fo.write("{\n")
			fo.write("\n")
			fo.write("\tAN(dst);\n")
			fo.write("\tAN(src);\n")
			for i in self.mbrs:
				fo.write("\tdst->" + i.arg)
				fo.write(" += src->" + i.arg + ";\n")
			fo.write("}\n")

#######################################################################

class directive(object):
	def __init__(self, s):
		ll = s.split("\n")
		i = ll.pop(0).split("::", 2)
		self.cmd = i[0]
		self.arg = i[1].strip()
		assert len(self.arg.split()) == 1

		self.param = {}
		while len(ll):
			j = ll[0].split(":",2)
			if len(j) != 3 or not j[0].isspace():
				break
			self.param[j[1]] = j[2].strip()
			ll.pop(0)
		self.ldoc = ll

	def getdoc(self):
		while len(self.ldoc) and self.ldoc[0].strip() == "":
			self.ldoc.pop(0)
		while len(self.ldoc) and self.ldoc[-1].strip() == "":
			self.ldoc.pop(-1)
		return self.ldoc

	def moredoc(self, s):
		self.getdoc()
		self.ldoc += s.split("\n")

	def emit_rst(self, fo):
		fo.write("\n.. " + self.cmd + ":: " + self.arg + "\n")
		self.emit_rst_doc(fo)

	def emit_rst_doc(self, fo):
		fo.write("\n".join(self.ldoc))

	def emit_h(self, fo):
		return

class rst_vsc_begin(directive):
	def __init__(self, s):
		super(rst_vsc_begin, self).__init__(s)

	def vscset(self, ss):
		ss.append(vscset(self.arg, self))

	def emit_rst(self, fo):
		fo.write("\n..\n\t" + self.cmd + ":: " + self.arg + "\n")

		s = self.arg.upper() + " – " + self.param["oneliner"]
		fo.write("\n")
		fo.write(s + "\n")
		fo.write("=" * len(s) + "\n")

		self.emit_rst_doc(fo)

class rst_vsc(directive):
	def __init__(self, s):
		super(rst_vsc, self).__init__(s)

		for i,v in PARAMS.iteritems():
			if v is not True:
				self.do_default(i, v[0], v[1])

		for p in self.param:
			if p in PARAMS:
				continue
			sys.stderr.write("Unknown parameter ")
			sys.stderr.write("'" + p + "'")
			sys.stderr.write(" on field '" + self.arg + "'\n")
			exit(2)

	def do_default(self, p, v, s):
		if p not in self.param:
			self.param[p] = v
		if self.param[p] not in s:
			sys.stderr.write("Wrong " + p + " '" + self.param[p])
			sys.stderr.write("' on field '" + self.arg + "'\n")
			exit(2)


	def emit_rst(self, fo):
		fo.write("\n``%s`` – " % self.arg)
		fo.write("`%s` - " % self.param["type"])
		fo.write("%s\n\n" % self.param["level"])

		fo.write("\t" + self.param["oneliner"] + "\n")
		self.emit_rst_doc(fo)

	def vscset(self, ss):
		ss[-1].addmbr(self)


class rst_vsc_end(directive):
	def __init__(self, s):
		super(rst_vsc_end, self).__init__(s)

	def vscset(self, ss):
		ss[-1].complete()

	def emit_rst(self, fo):
		fo.write("\n..\n\t" + self.cmd + ":: " + self.arg + "\n")
		self.emit_rst_doc(fo)

class other(object):
	def __init__(self, s):
		self.s = s

	def emit_rst(self, fo):
		fo.write(self.s)

	def emit_h(self, fo):
		return

	def vscset(self, ss):
		return

#######################################################################

class vsc_file(object):
	def __init__(self, fin):
		self.c = []
		scs = open(fin).read().split("\n.. ")
		self.c.append(other(scs[0]))
		ld = None
		for i in scs[1:]:
			j = i.split(None, 1)
			f = {
				"varnish_vsc_begin::":	rst_vsc_begin,
				"varnish_vsc::":	rst_vsc,
				"varnish_vsc_end::":	rst_vsc_end,
			}.get(j[0])
			if f is None:
				s = "\n.. " + i
				o = other(s)
				if ld is not None:
					ld.moredoc(s)
			else:
				o = f(i)
				ld = o
			self.c.append(o)

		self.vscset = []
		for i in self.c:
			i.vscset(self.vscset)

	def emit_h(self):
		for i in self.vscset:
			i.emit_h()

	def emit_c(self):
		for i in self.vscset:
			i.emit_c()

	def emit_rst(self):
		fo = sys.stdout
		for i in self.c:
			i.emit_rst(fo)

#######################################################################

if __name__ == "__main__":

	optlist, args = getopt.getopt(sys.argv[1:], "chr")

	fo = sys.stdout

	if len(args) != 1:
		sys.stderr.write("Need exactly one filename argument\n")
		exit(2)

	vf = vsc_file(args[0])
	for f,v in optlist:
		if f == '-r':
			vf.emit_rst()
		if f == '-h':
			vf.emit_h()
		if f == '-c':
			vf.emit_c()
