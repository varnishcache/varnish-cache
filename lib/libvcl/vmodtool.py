#!/usr/bin/env python
#-
# Copyright (c) 2010-2011 Varnish Software AS
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
	'HEADER':	"const struct gethdr_s *",
	'INT':		"VCL_INT",
	'IP':		"VCL_IP",
	'PRIV_CALL':	"struct vmod_priv *",
	'PRIV_VCL':	"struct vmod_priv *",
	'REAL':		"VCL_REAL",
	'STRING':	"VCL_STRING",
	'STRING_LIST':	"const char *, ...",
	'TIME':		"VCL_TIME",
	'VOID':		"VCL_VOID",
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
	def __init__(self, nam):
		if not is_c_name(nam):
			raise Exception("Module name '%s' is illegal" % nam)
		self.nam = nam
		self.init = None
		self.funcs = list()
		self.objs = list()

	def set_init(self, nam):
		if self.init != None:
			raise Exception("Module %s already has Init" % self.nam)
		if not is_c_name(nam):
			raise Exception("Init name '%s' is illegal" % nam)
		self.init = nam

	def add_func(self, fn):
		self.funcs.append(fn)

	def add_obj(self, obj):
		self.objs.append(obj)
		obj.set_modnam(self.nam)

	def c_proto(self, fo):
		for o in self.objs:
			o.c_proto(fo)
			fo.write("\n")
		for f in self.funcs:
			f.c_proto(fo)
		if self.init != None:
			fo.write("\n")
			fo.write("int " + self.init)
			fo.write(
			    "(struct vmod_priv *, const struct VCL_conf *);\n")
		#fo.write("\n")
		#fo.write("extern const void * const Vmod_" + self.nam + "_Id;\n")

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
		fo.write('const char Vmod_' + self.nam + '_Name[] =')
		fo.write(' \"' + self.nam + '";\n')
		fo.write("\n")

		cs = self.c_struct()
		fo.write("const " + cs + ' Vmod_' + self.nam + '_Func = ')
		fo.write(self.c_initializer())
		fo.write("\n")

		fo.write("\n")
		fo.write("const int Vmod_" + self.nam + '_Len =')
		fo.write(" sizeof(Vmod_" + self.nam + "_Func);\n")
		fo.write("\n")

		fo.write("const char Vmod_" + self.nam + "_Proto[] =\n")
		for t in self.c_typedefs_():
			fo.write('\t"' + t + '\\n"\n')
		fo.write('\t"\\n"\n')
		for i in (cs + " Vmod_" + self.nam + '_Func;').split("\n"):
			fo.write('\n\t"' + i + '\\n"')
		fo.write(";\n\n")

		fo.write(self.c_strspec())

		fo.write("\n")
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
		s = "const char * const Vmod_" + self.nam + "_Spec[] = {\n"

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

#######################################################################

class obj(object):
	def __init__(self, nam):
		self.nam = nam
		self.init = None
		self.fini = None
		self.methods = list()

	def set_modnam(self, modnam):
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

f = open(specfile, "r")
tl = list()
lines = list()
ln = 0
for l in f:
	ln += 1
	lines.append(l)
	if l == "":
		continue
	l = re.sub("[ \t]*#.*$", "", l)
	l = re.sub("[ \t]*\n", "", l)
	l = re.sub("([(){},])", r' \1 ', l)
	if l == "":
		continue
	for j in l.split():
		tl.append(token(ln, 0, j))
f.close()

#######################################################################
#
#
def parse_enum2(tl):
	t = tl.pop(0)
	if t.str != "{":
		raise Exception("expected \"{\"")
	s = "ENUM\\0"
	while True:
		t = tl.pop(0)
		if t.str == "}":
			break
		s += t.str + "\\0"
		if tl[0].str == ",":
			tl.pop(0)
		elif tl[0].str != "}":
			raise Exception("Expceted \"}\" or \",\"")
	s += "\\0"
	return arg("ENUM", det=s)

#######################################################################
#
#

def parse_func(tl, rt_type = None, obj=None):
	al = list()
	if rt_type == None:
		t = tl.pop(0)
		rt_type = t.str
	if rt_type not in ctypes:
		raise Exception(
		    "Return type '%s' not a valid type" % rt_type)

	t = tl.pop(0)
	fname = t.str
	if obj != None and fname[0] == "." and is_c_name(fname[1:]):
		fname = obj + fname
	elif not is_c_name(fname):
		raise Exception("Function name '%s' is illegal" % fname)

	t = tl.pop(0)
	if t.str != "(":
		raise Exception("Expected \"(\" got \"%s\"", t.str)

	while True:
		t = tl.pop(0)
		if t.str == ")":
			break
		if t.str == "ENUM":
			al.append(parse_enum2(tl))
		elif t.str in ctypes:
			al.append(arg(t.str))
		else:
			raise Exception("ARG? %s" % t.str)
		if is_c_name(tl[0].str):
			al[-1].nam = tl[0].str
			t = tl.pop(0)
		if tl[0].str == ",":
			tl.pop(0)
		elif tl[0].str != ")":
			raise Exception("Expceted \")\" or \",\"")
	if t.str != ")":
		raise Exception("End Of Input looking for ')'")
	f = func(fname, rt_type, al)
	return f

#######################################################################
#
#

def parse_obj(tl):
	o = obj(tl[0].str)
	f = parse_func(tl, "VOID")
	o.set_init(f)
	t = tl.pop(0)
	assert t.str == "{"
	while True:
		t = tl.pop(0)
		if t.str == "}":
			break
		assert t.str == "Method"
		f = parse_func(tl, obj=o.nam)
		o.add_method(f)
	return o

#######################################################################
# The first thing in the file must be the Module declaration
#

t = tl.pop(0)
if t.str != "Module":
	raise Exception("\"Module\" must be first in file")
t = tl.pop(0)
vmod = vmod(t.str)

#######################################################################
# Parse the rest of the file
#

while len(tl) > 0:
	t = tl.pop(0)

	if t.str == "Init":
		t = tl.pop(0)
		vmod.set_init(t.str)
	elif t.str == "Function":
		f = parse_func(tl)
		vmod.add_func(f)
	elif t.str == "Object":
		o = parse_obj(tl)
		vmod.add_obj(o)
	else:
		raise Exception("Expected \"Init\", \"Fini\" or \"Function\"")

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

vmod.c_proto(fh)

fc.write("""#include "config.h"

#include "vrt.h"
#include "vcc_if.h"
#include "vmod_abi.h"


""")

vmod.c_typedefs(fc)
vmod.c_vmod(fc)

fc.close()
fh.close()
