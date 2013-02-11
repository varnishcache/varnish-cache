#!/usr/local/bin/python
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

from __future__ import print_function

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
		self.fini = None
		self.funcs = list()
		self.objs = list()

	def set_init(self, nam):
		if self.init != None:
			raise Exception("Module %s already has Init" % self.nam)
		if not is_c_name(nam):
			raise Exception("Init name '%s' is illegal" % nam)
		self.init = nam

	def set_fini(self, nam):
		if self.fini != None:
			raise Exception("Module %s already has Fini" % self.nam)
		if not is_c_name(nam):
			raise Exception("Fini name '%s' is illegal" % nam)
		self.fini = nam

	def add_func(self, fn):
		self.funcs.append(fn)

	def add_obj(self, obj):
		self.objs.append(obj)
		obj.set_modnam(self.nam)

	def c_proto(self, fo):
		for o in self.objs:
			o.c_proto(fo)
		for f in self.funcs:
			f.c_proto(fo)
		if self.init != None:
			fo.write("int " + self.init)
			fo.write(
			    "(struct vmod_priv *, const struct VCL_conf *);\n")
		if self.fini != None:
			fo.write("int " + self.fini)
			fo.write(
			    "(struct vmod_priv *, const struct VCL_conf *);\n")
		fo.write("extern const void * const Vmod_Id;\n")

	def c_typedefs_(self):
		l = list()
		for o in self.objs:
			for t in o.c_typedefs(self.nam):
				l.append(t)
		l.append("")
		l.append("/* Functions */")
		for f in self.funcs:
			l.append(f.c_typedef(self.nam))
		return l

	def c_typedefs(self, fo):
		for i in self.c_typedefs_():
			fo.write(i + "\n")

	def c_vmod(self, fo):
		fo.write('const char Vmod_Name[] = \"' + self.nam + '";\n')
		fo.write("\n")

		cs = self.c_struct()
		fo.write("const " + cs + ' Vmod_Func = ')
		fo.write(self.c_initializer())
		fo.write("\n")

		fo.write("\n")
		fo.write("const int Vmod_Len = sizeof(Vmod_Func);\n")
		fo.write("\n")


		fo.write("const char Vmod_Proto[] =\n")
		for t in self.c_typedefs_():
			fo.write('\t"' + t + '\\n"\n')
		fo.write('\t"\\n"\n')
		for i in (cs + " Vmod_Func_" + self.nam + ';').split("\n"):
			fo.write('\n\t"' + i + '\\n"')
		fo.write(";\n\n")

		fo.write(self.c_strspec())

		fo.write("\n")
		fo.write('const char Vmod_Varnish_ABI[] = VMOD_ABI_Version;\n')
		fo.write("\n")
		fo.write('const void * const Vmod_Id = &Vmod_Id;\n')

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
		if self.fini != None:
			s += "\t" + self.fini + ",\n"
		s += "};"

		return s

	def c_struct(self):
		s = 'struct Vmod_Func_' + self.nam + ' {\n'
		for o in self.objs:
			s += o.c_struct(self.nam)

		s += "\n\t/* Functions */\n"
		for f in self.funcs:
			s += f.c_struct(self.nam)

		s += "\n\t/* Init/Fini */\n"
		if self.init != None:
			s += "\tvmod_init_f\t*_init;\n"
		if self.fini != None:
			s += "\tvmod_fini_f\t*_fini;\n"
		s += '}'
		return s

	def c_strspec(self):
		s = "const char * const Vmod_Spec[] = {\n"

		for o in self.objs:
			s += o.c_strspec(self.nam)

		s += "\n\t/* Functions */\n"
		for f in self.funcs:
			s += '\t"' + f.c_strspec(self.nam) + '",\n'

		s += "\n\t/* Init/Fini */\n"
		if self.init != None:
			s += '\t"INIT\\0Vmod_Func_' + self.nam + '._init",\n'
		if self.fini != None:
			s += '\t"FINI\\0Vmod_Func_' + self.nam + '._fini",\n'

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
		self.cnam = nam.replace(".", "__")
		self.al = al
		self.retval = retval
		self.pfx = None

	def __repr__(self):
		return "<FUNC %s %s>" % (self.retval, self.nam)

	def set_pfx(self, s):
		self.pfx = s

	def c_proto(self, fo):
		fo.write(ctypes[self.retval])
		fo.write(" vmod_" + self.cnam)
		fo.write("(struct req *")
		if self.pfx != None:
			fo.write(self.pfx)
		for a in self.al:
			fo.write(", " + ctypes[a.typ])
		fo.write(");\n")

	def c_typedef(self, modname):
		s = "typedef "
		s += ctypes[self.retval]
		s += " td_" + modname + "_" + self.cnam
		s += "(struct req *"
		if self.pfx != None:
			s += self.pfx
		for a in self.al:
			s += ", " + ctypes[a.typ]
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
		s += "Vmod_Func_" + modnam + "." + self.cnam + "\\0"
		s += self.retval + "\\0"
		for a in self.al:
			s += a.c_strspec()
		return s
		
#######################################################################

class obj(object):
	def __init__(self, nam):
		self.nam = nam
		self.init_fini = None
		self.methods = list()

	def set_modnam(self, modnam):
		self.st = "struct vmod_" + modnam + "_" + self.nam
		self.init_fini.set_pfx(", " + self.st + " **")
		for m in self.methods:
			m.set_pfx(", " + self.st + " *")

	def set_init_fini(self, f):
		self.init_fini = f

	def add_method(self, m):
		self.methods.append(m)

	def c_typedefs(self, modnam):
		l = list()
		l.append("/* Object " + self.nam + " */")
		l.append(self.init_fini.c_typedef(modnam) + "")
		for m in self.methods:
			l.append(m.c_typedef(modnam) + "")
		return l

	def c_proto(self, fo):
		fo.write(self.st + ";\n")
		self.init_fini.c_proto(fo)
		for m in o.methods:
			m.c_proto(fo)

	def c_struct(self, modnam):
		s = "\t/* Object " + self.nam + " */\n"
		s += self.init_fini.c_struct(modnam)
		for m in self.methods:
			s += m.c_struct(modnam)
		return s

	def c_initializer(self):
		s = "\t/* Object " + self.nam + " */\n"
		s += self.init_fini.c_initializer()
		for m in self.methods:
			s += m.c_initializer()
		return s

	def c_strspec(self, modnam):
		s = "\t/* Object " + self.nam + " */\n"
		s += '\t"OBJ\\0'
		s += self.st + '\\0'
		s += self.init_fini.c_strspec(modnam) + '",\n'
		for m in self.methods:
			s += '\t"METHOD\\0' + m.c_strspec(modnam) + '",\n'
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
	o.set_init_fini(f)
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
	elif t.str == "Fini":
		t = tl.pop(0)
		vmod.set_fini(t.str)
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

fh.write('struct req;\n')
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
fc.write("\n")
vmod.c_vmod(fc)

fc.close()
fh.close()
