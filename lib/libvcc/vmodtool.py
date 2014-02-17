#!/usr/bin/env python
#-
# Copyright (c) 2010-2014 Varnish Software AS
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
# Read the vmod.spec file and produce the vmod.h and vmod.c files.
#
# vmod.h contains the prototypes for the published functions, the module
# C-code should include this file to ensure type-consistency.
#
# vmod.c contains the symbols which VCC and varnishd will use to access
# the module:  A structure of properly typed function pointers, the
# size of this structure in bytes, and the definition of the structure
# as a string, suitable for inclusion in the C-source of the compile VCL
# program.

import sys
import re

if len(sys.argv) == 2:
	specfile = sys.argv[1]
else:
	specfile = "vmod.vcc"

ctypes = {
	'BACKEND':	"VCL_BACKEND",
	'BOOL':		"VCL_BOOL",
	'DURATION':	"VCL_DURATION",
	'ENUM':		"VCL_ENUM",
	'HEADER':	"VCL_HEADER",
	'INT':		"VCL_INT",
	'IP':		"VCL_IP",
	'PRIV_CALL':	"struct vmod_priv *",
	'PRIV_VCL':	"struct vmod_priv *",
	'REAL':		"VCL_REAL",
	'STRING':	"VCL_STRING",
	'STRING_LIST':	"const char *, ...",
	'TIME':		"VCL_TIME",
	'VOID':		"VCL_VOID",
	'BLOB':		"VCL_BLOB",
}

#######################################################################

def file_header(fo):
        fo.write("""/*
 * NB:  This file is machine generated, DO NOT EDIT!
 *
 * Edit vmod.vcc and run vmod.py instead
 */

""")

#######################################################################

def is_c_name(s):
	return None != re.match("^[a-z][a-z0-9_]*$", s)

#######################################################################

class token(object):
	def __init__(self, ln, ch, str):
		self.ln = ln
		self.ch = ch
		self.str = str

	def __repr__(self):
		return "<@%d \"%s\">" % (self.ln, self.str)

#######################################################################

class vmod(object):
	def __init__(self, nam, dnam, sec):
		if not is_c_name(nam):
			raise Exception("Module name '%s' is illegal" % nam)
		self.nam = nam
		self.dnam = dnam
		self.sec = sec
		self.init = None
		self.funcs = list()
		self.objs = list()
		self.doc_str = []
		self.doc_order = []

	def set_init(self, nam):
		if self.init != None:
			raise Exception("Module %s already has Init" % self.nam)
		if not is_c_name(nam):
			raise Exception("Init name '%s' is illegal" % nam)
		self.init = nam

	def add_func(self, fn):
		self.funcs.append(fn)
		self.doc_order.append(fn)

	def add_obj(self, obj):
		self.objs.append(obj)
		self.doc_order.append(obj)

	def c_proto(self, fo):
		for o in self.objs:
			o.fixup(self.nam)
			o.c_proto(fo)
			fo.write("\n")
		for f in self.funcs:
			f.c_proto(fo)
		if self.init != None:
			fo.write("\n")
			fo.write("int " + self.init)
			fo.write(
			    "(struct vmod_priv *, const struct VCL_conf *);\n")

	def c_typedefs_(self):
		l = list()
		for o in self.objs:
			for t in o.c_typedefs(self.nam):
				l.append(t)
			l.append("")
		l.append("/* Functions */")
		for f in self.funcs:
			l.append(f.c_typedef(self.nam))
		l.append("")
		return l

	def c_typedefs(self, fo):
		for i in self.c_typedefs_():
			fo.write(i + "\n")

	def c_vmod(self, fo):
		fo.write('extern const char Vmod_' + self.nam + '_Name[];\n')
		fo.write('const char Vmod_' + self.nam + '_Name[] =')
		fo.write(' \"' + self.nam + '";\n')
		fo.write("\n")

		cs = self.c_struct()
		fo.write(cs + ';\n')

		vfn = 'Vmod_' + self.nam + '_Func';

		fo.write("extern const struct " + vfn + " " + vfn + ';\n')
		fo.write("const struct " + vfn + " " + vfn + ' =')
		fo.write(self.c_initializer())
		fo.write("\n")

		fo.write("\n")
		fo.write("extern const int Vmod_" + self.nam + '_Len;\n')
		fo.write("const int Vmod_" + self.nam + '_Len =')
		fo.write(" sizeof(Vmod_" + self.nam + "_Func);\n")
		fo.write("\n")

		fo.write("extern const char Vmod_" + self.nam + "_Proto[];\n")
		fo.write("const char Vmod_" + self.nam + "_Proto[] =\n")
		for t in self.c_typedefs_():
			fo.write('\t"' + t + '\\n"\n')
		fo.write('\t"\\n"\n')
		for i in (cs + ";").split("\n"):
			fo.write('\n\t"' + i + '\\n"')
		fo.write('\n\t"static struct ' + vfn + " " + vfn + ';";\n\n')

		fo.write(self.c_strspec())

		fo.write("\n")
		fo.write('extern const char Vmod_' + self.nam + '_ABI[];\n')
		fo.write('const char Vmod_' + self.nam + '_ABI[] =')
		fo.write(' VMOD_ABI_Version;\n')
		#fo.write("\n")
		#fo.write('const void * const Vmod_' + self.nam + '_Id =')
		#fo.write(' &Vmod_' + self.nam + '_Id;\n')

	def c_initializer(self):
		s = '{\n'
		for o in self.objs:
			s += o.c_initializer()

		s += "\n\t/* Functions */\n"
		for f in self.funcs:
			s += f.c_initializer()

		s += "\n\t/* Init/Fini */\n"
		if self.init != None:
			s += "\t" + self.init + ",\n"
		s += "};"

		return s

	def c_struct(self):
		s = 'struct Vmod_' + self.nam + '_Func {\n'
		for o in self.objs:
			s += o.c_struct(self.nam)

		s += "\n\t/* Functions */\n"
		for f in self.funcs:
			s += f.c_struct(self.nam)

		s += "\n\t/* Init/Fini */\n"
		if self.init != None:
			s += "\tvmod_init_f\t*_init;\n"
		s += '}'
		return s

	def c_strspec(self):
		s = "const char * const Vmod_" + self.nam + "_Spec[]"
		s = "extern " + s + ";\n" + s + " = {\n"

		for o in self.objs:
			s += o.c_strspec(self.nam)

		s += "\n\t/* Functions */\n"
		for f in self.funcs:
			s += '\t"' + f.c_strspec(self.nam) + '",\n'

		s += "\n\t/* Init/Fini */\n"
		if self.init != None:
			s += '\t"INIT\\0Vmod_' + self.nam + '_Func._init",\n'

		s += "\t0\n"
		s += "};\n"
		return s

	def doc(self, l):
		self.doc_str.append(l)

	def doc_dump(self, fo, suf):
		i = "vmod_" + self.nam
		fo.write("=" * len(i) + "\n")
		fo.write(i + "\n")
		fo.write("=" * len(i) + "\n")
		fo.write("\n")
		i = self.dnam
		fo.write("-" * len(i) + "\n")
		fo.write(i + "\n")
		fo.write("-" * len(i) + "\n")
		fo.write("\n")
		fo.write(":Manual section: %s\n" % self.sec)
		fo.write("\n")
		fo.write("SYNOPSIS\n")
		fo.write("========\n")
		fo.write("\n")
		fo.write("import %s [from \"path\"] ;\n" % self.nam)
		fo.write("\n")
		for i in self.doc_str:
			fo.write(i + "\n")
		fo.write("CONTENTS\n")
		fo.write("========\n")
		fo.write("\n")
		l = []
		for i in self.funcs:
			l.append(i.doc_idx(suf))
		for i in self.objs:
			l += i.doc_idx(suf)
		l.sort()
		for i in l:
			fo.write("* " + i[1] + "\n")
		fo.write("\n")
		for i in self.doc_order:
			i.doc_dump(fo)

#######################################################################

class func(object):
	def __init__(self, nam, retval, al):
		#if not is_c_name(nam):
		#	raise Exception("Func name '%s' is illegal" % nam)
		if retval not in ctypes:
			raise Exception(
			    "Return type '%s' not a valid type" % retval)
		self.nam = nam
		self.cnam = nam.replace(".", "_")
		self.al = al
		self.retval = retval
		self.pfx = None
		self.doc_str = []

	def __repr__(self):
		return "<FUNC %s %s>" % (self.retval, self.nam)

	def set_pfx(self, s):
		self.pfx = s

	def c_proto(self, fo, fini=False):
		fo.write(ctypes[self.retval])
		fo.write(" vmod_" + self.cnam + "(")
		p = ""
		if not fini:
			fo.write("const struct vrt_ctx *")
			p = ", "
		if self.pfx != None:
			fo.write(p + self.pfx)
			p = ", "
		for a in self.al:
			fo.write(p + ctypes[a.typ])
			p = ", "
			if a.nam != None:
				fo.write(" " + a.nam)
		fo.write(");\n")

	def c_typedef(self, modname, fini=False):
		s = "typedef "
		s += ctypes[self.retval]
		s += " td_" + modname + "_" + self.cnam + "("
		p = ""
		if not fini:
			s += "const struct vrt_ctx *"
			p = ", "
		if self.pfx != None:
			s += p + self.pfx
			p = ", "
		for a in self.al:
			s += p + ctypes[a.typ]
			p = ", "
		s += ");"
		return s

	def c_struct(self, modname):
		s = '\ttd_' + modname + "_" + self.cnam
		while len(s.expandtabs()) < 40:
			s += "\t"
		s += "*" + self.cnam + ";\n"
		return s

	def c_initializer(self):
		return "\tvmod_" + self.cnam + ",\n"

	def c_strspec(self, modnam):
		s = modnam + "." + self.nam
		s += "\\0"
		s += "Vmod_" + modnam + "_Func." + self.cnam + "\\0"
		s += self.retval + "\\0"
		for a in self.al:
			s += a.c_strspec()
		return s

	def doc(self, l):
		self.doc_str.append(l)

	def doc_proto(self):
		s = self.retval + " " + self.nam + "("
		d = ""
		for i in self.al:
			s += d + i.typ
			d = ", "
		s += ")"
		return s

	def doc_idx(self, suf):
		if suf == "":
			return (self.nam, ":ref:`func_" + self.nam + "`")
		else:
			return (self.nam, self.doc_proto())

	def doc_dump(self, fo):
		s = self.doc_proto()
		fo.write(".. _func_" + self.nam + ":\n\n")
		fo.write(s + "\n")
		fo.write("-" * len(s) + "\n")
		fo.write("\n")
		fo.write("Prototype\n")
		s = "\t" + self.retval + " " + self.nam + "("
		d = ""
		for i in self.al:
			s += d + i.typ
			if i.nam != None:
				s += " " + i.nam
			d = ", "
		fo.write(s + ")\n")
		for i in self.doc_str:
			fo.write(i + "\n")


#######################################################################

class obj(object):
	def __init__(self, nam):
		self.nam = nam
		self.init = None
		self.fini = None
		self.methods = list()
		self.doc_str = []

	def fixup(self, modnam):
		assert self.nam != None
		self.st = "struct vmod_" + modnam + "_" + self.nam
		self.init.set_pfx(self.st + " **, const char *")
		self.fini.set_pfx(self.st + " **")
		for m in self.methods:
			m.set_pfx(self.st + " *")

	def set_init(self, f):
		self.init = f
		self.fini = func(f.nam, "VOID", [])
		self.init.cnam += "__init"
		self.fini.cnam += "__fini"

	def add_method(self, m):
		self.methods.append(m)

	def c_typedefs(self, modnam):
		l = list()
		l.append("/* Object " + self.nam + " */")
		l.append(self.st + ";")
		l.append(self.init.c_typedef(modnam) + "")
		l.append(self.fini.c_typedef(modnam, fini=True) + "")
		for m in self.methods:
			l.append(m.c_typedef(modnam) + "")
		return l

	def c_proto(self, fo):
		fo.write(self.st + ";\n")
		self.init.c_proto(fo)
		self.fini.c_proto(fo, fini = True)
		for m in self.methods:
			m.c_proto(fo)

	def c_struct(self, modnam):
		s = "\t/* Object " + self.nam + " */\n"
		s += self.init.c_struct(modnam)
		s += self.fini.c_struct(modnam)
		for m in self.methods:
			s += m.c_struct(modnam)
		return s

	def c_initializer(self):
		s = "\t/* Object " + self.nam + " */\n"
		s += self.init.c_initializer()
		s += self.fini.c_initializer()
		for m in self.methods:
			s += m.c_initializer()
		return s

	def c_strspec(self, modnam):
		s = "\t/* Object " + self.nam + " */\n"
		s += '\t"OBJ\\0"\n'
		s += '\t\t"' + self.init.c_strspec(modnam) + '\\0"\n'
		s += '\t\t"' + self.st + '\\0"\n'
		s += '\t\t"' + self.fini.c_strspec(modnam) + '\\0"\n'
		for m in self.methods:
			s += '\t\t"' + m.c_strspec(modnam) + '\\0"\n'
		s += '\t\t"\\0",\n'
		return s

	def doc(self, l):
		self.doc_str.append(l)

	def doc_idx(self, suf):
		l = []
		if suf == "":
			l.append((self.nam, ":ref:`obj_" + self.nam + "`"))
		else:
			l.append((self.nam, "Object " + self.nam))
		for i in self.methods:
			l.append(i.doc_idx(suf))
		return l

	def doc_dump(self, fo):
		fo.write(".. _obj_" + self.nam + ":\n\n")
		s = "Object " + self.nam
		fo.write(s + "\n")
		fo.write("=" * len(s) + "\n")
		fo.write("\n")

		for i in self.doc_str:
			fo.write(i + "\n")

		for i in self.methods:
			i.doc_dump(fo)

#######################################################################

class arg(object):
	def __init__(self, typ, nam = None, det = None):
		self.nam = nam
		self.typ = typ
		self.det = det

	def __repr__(self):
		return "<ARG %s %s %s>" % (self.nam, self.typ, str(self.det))

	def c_strspec(self):
		if self.det == None:
			return self.typ + "\\0"
		else:
			return self.det
		return "??"

#######################################################################
#
#
def parse_enum2(tl):
	t = tl.get_token()
	if t.str != "{":
		raise Exception("expected \"{\"")
	s = "ENUM\\0"
	t = None
	while True:
		if t == None:
			t = tl.get_token()
		if t.str == "}":
			break
		s += t.str + "\\0"
		t = tl.get_token()
		if t.str == ",":
			t = None
		elif t.str == "}":
			break
		else:
			raise Exception(
			    "Expected \"}\" or \",\" not \"%s\"" % t.str)
	s += "\\0"
	return arg("ENUM", det=s)

#######################################################################
#
#

def parse_module(tl):
	nm = tl.get_token().str
	sec = tl.get_token().str
	s = ""
	while len(tl.tl) > 0:
		s += " " + tl.get_token().str
	dnm = s[1:]
	return vmod(nm, dnm, sec)

#######################################################################
#
#

def parse_func(tl, rt_type = None, obj=None):
	al = list()
	if rt_type == None:
		t = tl.get_token()
		rt_type = t.str
	if rt_type not in ctypes:
		raise Exception(
		    "Return type '%s' not a valid type" % rt_type)

	t = tl.get_token()
	fname = t.str
	if obj != None and fname[0] == "." and is_c_name(fname[1:]):
		fname = obj + fname
	elif not is_c_name(fname):
		raise Exception("Function name '%s' is illegal" % fname)

	t = tl.get_token()
	if t.str != "(":
		raise Exception("Expected \"(\" got \"%s\"", t.str)

	t = None
	while True:
		if t == None:
			t = tl.get_token()
		assert t != None

		if t.str == "ENUM":
			al.append(parse_enum2(tl))
		elif t.str in ctypes:
			al.append(arg(t.str))
		elif t.str == ")":
			break
		else:
			raise Exception("ARG? %s" % t.str)
		t = tl.get_token()
		if is_c_name(t.str):
			al[-1].nam = t.str
			t = None
		elif t.str == ",":
			t = None
		elif t.str == ")":
			break
		else:
			raise Exception(
			    "Expceted \")\" or \",\" not \"%s\"" % t.str)
	if t.str != ")":
		raise Exception("End Of Input looking for ')'")
	f = func(fname, rt_type, al)

	return f

#######################################################################
#
#

def parse_obj(tl):
	f = parse_func(tl, "VOID")
	o = obj(f.nam)
	o.set_init(f)
	return o


#######################################################################
# A section of the specfile, starting at a keyword

class file_section(object):
	def __init__(self):
		self.l = []
		self.tl = []

	def add_line(self, ln, l):
		self.l.append((ln, l))

	def get_token(self):
		while True:
			if len(self.tl) > 0:
				# print("T\t", self.tl[0])
				return self.tl.pop(0)
			if len(self.l) == 0:
				break
			self.more_tokens()
		return None

	def more_tokens(self):
		ln,l = self.l.pop(0)
		if l == "":
			return
		l = re.sub("[ \t]*#.*$", "", l)
		l = re.sub("[ \t]*\n", "", l)
		l = re.sub("([(){},])", r' \1 ', l)
		if l == "":
			return
		for j in l.split():
			self.tl.append(token(ln, 0, j))

	def parse(self, vx):
		t = self.get_token()
		if t == None:
			return
		t0 = t.str
		if t.str == "$Module":
			o = parse_module(self)
			vx.append(o)
		elif t.str == "$Init":
			x = self.get_token()
			vx[0].set_init(x.str)
			o = None
		elif t.str == "$Function":
			if len(vx) == 2:
				vx.pop(-1)
			o = parse_func(self)
			vx[0].add_func(o)
		elif t.str == "$Object":
			if len(vx) == 2:
				vx.pop(-1)
			o = parse_obj(self)
			vx[0].add_obj(o)
			vx.append(o)
		elif t.str == "$Method":
			if len(vx) != 2:
				raise Exception("$Method outside $Object")
			o = parse_func(self, obj = vx[1].nam)
			vx[1].add_method(o)
		else:
			raise Exception("Unknown keyword: " + t.str)
		assert len(self.tl) == 0
		if o == None:
			print("Warning:")
			print("%s description is not included in .rst:" %t0)
			for ln,i in self.l:
				print("\t", i)
		else:
			for ln,i in self.l:
				o.doc(i)

#######################################################################
# Polish the copyright message
#
def polish(l):
	if len(l[0]) == 0:
		l.pop(0)
		return True
	c = l[0][0]
	for i in l:
		if len(i) == 0:
			continue
		if i[0] != c:
			c = None
			break
	if c != None:
		for i in range(len(l)):
			l[i] = l[i][1:]
		return True
	return False

#######################################################################
# Read the file in

f = open(specfile, "r")
lines = []
for i in f:
	lines.append(i.rstrip())
f.close()
ln = 0

#######################################################################
# First collect the copyright:  All initial lines starting with '#'

copyright = []
while len(lines[0]) > 0 and lines[0][0] == "#":
	ln += 1
	copyright.append(lines.pop(0))

if len(copyright) > 0:
	if copyright[0] == "#-":
		copyright = [ ]
	else:
		while polish(copyright):
			continue

if False:
	for i in copyright:
		print("(C)\t", i)

#######################################################################
# Break into sections

keywords = {
	"$Module":	True,
	"$Function":	True,
	"$Object":	True,
	"$Method":	True,
	"$Init":	True,
}

sl = []
sc = file_section()
sl.append(sc)
while len(lines) > 0:
	ln += 1
	l = lines.pop(0)
	j = l.split()
	if len(j) > 0 and j[0] in keywords:
		sc = file_section()
		sl.append(sc)
	sc.add_line(ln,l)

#######################################################################
# Parse each section

first = True

vx = []
for i in sl:
	i.parse(vx)
	assert len(i.tl) == 0

#######################################################################
# Parsing done, now process
#

fc = open("vcc_if.c", "w")
fh = open("vcc_if.h", "w")

file_header(fc)
file_header(fh)

fh.write('struct vrt_ctx;\n')
fh.write('struct VCL_conf;\n')
fh.write('struct vmod_priv;\n')
fh.write("\n");

vx[0].c_proto(fh)

fc.write("""#include "config.h"

#include "vrt.h"
#include "vcc_if.h"
#include "vmod_abi.h"


""")

vx[0].c_typedefs(fc)
vx[0].c_vmod(fc)

fc.close()
fh.close()

for suf in ("", ".man"):
	fr = open("vmod_" + vx[0].nam + suf + ".rst", "w")
	vx[0].doc_dump(fr, suf)

	if len(copyright) > 0:
		fr.write("\n")
		fr.write("COPYRIGHT\n")
		fr.write("=========\n")
		fr.write("\n::\n\n")
		for i in copyright:
			fr.write("  " + i + "\n")
		fr.write("\n")

	fr.close()
