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

class vmod(object):
	def __init__(self, nam):
		if not is_c_name(nam):
			raise Exception("Module name '%s' is illegal" % nam)
		self.nam = nam
		self.init = None
		self.fini = None
		self.funcs = list()

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

	def c_proto(self, fo):
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

	def c_typedefs(self, fo):
		for f in self.funcs:
			fo.write(f.c_typedefs(fo, self.nam) + "\n")

	def c_vmod(self, fo):
		fo.write('const char Vmod_Name[] = \"' + self.nam + '";\n')
		fo.write("\n")

		cs = self.c_struct()
		fo.write("const " + cs + ' Vmod_Func = {\n')

		for f in self.funcs:
			fo.write("\tvmod_" + f.nam + ",\n")
		if self.init != None:
			fo.write("\t" + self.init + ",\n")
		if self.fini != None:
			fo.write("\t" + self.fini + ",\n")
		fo.write("};\n")
		fo.write("\n")
		fo.write("const int Vmod_Len = sizeof(Vmod_Func);\n")
		fo.write("\n")
		fo.write("const char Vmod_Proto[] =\n")
		for f in self.funcs:
			fo.write('\t"' + f.c_typedefs(fo, self.nam) + '\\n"\n')
		fo.write('\t"\\n"\n')
		for i in (cs + " Vmod_Func_" + self.nam + ';').split("\n"):
			fo.write('\n\t"' + i + '\\n"')
		fo.write(";\n\n")

		fo.write("const char * const Vmod_Spec[] = {\n")
		for f in self.funcs:
			fo.write('\t"' + f.strspec(self.nam) + '",\n')
		if self.init != None:
			fo.write(
			    '\t"INIT\\0Vmod_Func_' + self.nam + '._init",\n')
		if self.fini != None:
			fo.write(
			    '\t"FINI\\0Vmod_Func_' + self.nam + '._fini",\n')
		fo.write("\t0\n")
		fo.write("};\n")
		fo.write("\n")
		fo.write('const char Vmod_Varnish_ABI[] = VMOD_ABI_Version;\n')
		fo.write("\n")
		fo.write('const void * const Vmod_Id = &Vmod_Id;\n')

	def c_struct(self):
		s = 'struct Vmod_Func_' + self.nam + ' {\n'
		for f in self.funcs:
			s += '\ttd_' + self.nam + "_" + f.nam
			s += "\t*" + f.nam + ";\n"
		if self.init != None:
			s += "\tvmod_init_f\t*_init;\n"
		if self.fini != None:
			s += "\tvmod_fini_f\t*_fini;\n"
		s += '}'
		return s
	

class func(object):
	def __init__(self, nam, retval, al):
		if not is_c_name(nam):
			raise Exception("Func name '%s' is illegal" % nam)
		if retval not in ctypes:
			raise Exception(
			    "Return type '%s' not a valid type" % retval)
		self.nam = nam
		self.al = al
		self.retval = retval

	def __repr__(self):
		return "<FUNC %s %s>" % (self.retval, self.nam)

	def c_proto(self, fo):
		fo.write(ctypes[self.retval])
		fo.write(" vmod_" + self.nam)
		fo.write("(struct req *")
		for a in self.al:
			fo.write(", " + ctypes[a.typ])
		fo.write(");\n")

	def c_typedefs(self, fo, modname):
		s = "typedef "
		s += ctypes[self.retval]
		s += " td_" + modname + "_" + self.nam
		s += "(struct req *"
		for a in self.al:
			s += ", " + ctypes[a.typ]
		s += ");"
		return s

	def strspec(self, modname):
		s = modname + "." + self.nam
		s += "\\0"
		s += "Vmod_Func_" + modname + "." + self.nam + "\\0"
		s += self.retval + "\\0"
		for a in self.al:
			s += a.strspec()
		return s
		

class arg(object):
	def __init__(self, typ, nam = None, det = None):
		self.nam = nam
		self.typ = typ
		self.det = det

	def __repr__(self):
		return "<ARG %s %s %s>" % (self.nam, self.typ, str(self.det))

	def strspec(self):
		if self.det == None:
			return self.typ + "\\0"
		else:
			return self.det
		return "??"

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
		continue

	if t.str == "Fini":
		t = tl.pop(0)
		vmod.set_fini(t.str)
		continue

	if t.str == "Function":
		al = list()
		t = tl.pop(0)
		rt_type = t.str
		if rt_type not in ctypes:
			raise Exception(
			    "Return type '%s' not a valid type" % rt_type)

		t = tl.pop(0)
		fname = t.str
		if not is_c_name(fname):
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
		vmod.add_func(f)
		continue

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
