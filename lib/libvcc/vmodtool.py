#!/usr/bin/env python
#-
# Copyright (c) 2010-2015 Varnish Software AS
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
Read the vmod.vcc file (inputvcc) and produce:
	vmod_if.h -- Prototypes for the implementation
	vmod_if.c -- Magic glue & datastructures to make things a VMOD.
	vmod_${name}.rst -- Extracted documentation
"""

# This script should work with both Python 2 and Python 3.
from __future__ import print_function

import sys
import re
import optparse
import unittest
import random
from os import unlink
from os.path import dirname, realpath, exists
from pprint import pprint, pformat

ctypes = {
	'BACKEND':	"VCL_BACKEND",
	'BLOB':		"VCL_BLOB",
	'BOOL':		"VCL_BOOL",
	'BYTES':	"VCL_BYTES",
	'DURATION':	"VCL_DURATION",
	'ENUM':		"VCL_ENUM",
	'HEADER':	"VCL_HEADER",
	'HTTP':		"VCL_HTTP",
	'INT':		"VCL_INT",
	'IP':		"VCL_IP",
	'PRIV_CALL':	"struct vmod_priv *",
	'PRIV_VCL':	"struct vmod_priv *",
	'PRIV_TASK':	"struct vmod_priv *",
	'PRIV_TOP':	"struct vmod_priv *",
	'PROBE':	"VCL_PROBE",
	'REAL':		"VCL_REAL",
	'STRING':	"VCL_STRING",
	'STRING_LIST':	"const char *, ...",
	'TIME':		"VCL_TIME",
	'VOID':		"VCL_VOID",
}

#######################################################################

def write_file_warning(fo, a, b, c):
	fo.write(a + "\n")
	fo.write(b + " NB:  This file is machine generated, DO NOT EDIT!\n")
	fo.write(b + "\n")
	fo.write(b + " Edit vmod.vcc and run make instead\n")
	fo.write(c + "\n\n")

def write_c_file_warning(fo):
	write_file_warning(fo, "/*", " *", " */")

def write_rst_file_warning(fo):
	write_file_warning(fo, "..", "..", "..")

#######################################################################

def lwrap(s, w=72):
	"""
	Wrap a c-prototype like string into a number of lines
	"""
	l = []
	p = ""
	while len(s) > w:
		y = s[:w].rfind(',')
		if y == -1:
			y = s[:w].rfind('(')
		if y == -1:
			break
		l.append(p + s[:y + 1])
		s = s[y + 1:].lstrip()
		p = "    "
	if len(s) > 0:
		l.append(p + s)
	return l

def quote(s):
	t = ""
	for i in s:
		if i == '"':
			t += '\\"'
		else:
			t += i
	return t

#######################################################################

def is_c_name(s):
	return None != re.match("^[a-zA-Z][a-zA-Z0-9_]*$", s)


class ParseError(Exception):
	"An error reading the input file."
	pass

class FormatError(Exception):
	"""
		Raised if the content of the (otherwise well-formed) input file
		is invalid.
	"""
	def __init__(self, msg, details):
		self.msg = msg
		self.details = details
		Exception.__init__(self)


#######################################################################

class Token(object):
	def __init__(self, ln, ch, tokstr):
		self.ln = ln
		self.ch = ch
		self.str = tokstr

	def __repr__(self):
		return "<@%d \"%s\">" % (self.ln, self.str)

#######################################################################

class Vmod(object):
	def __init__(self, nam, dnam, sec):
		if not is_c_name(nam):
			raise ParseError("Module name '%s' is illegal", nam)
		self.nam = nam
		self.dnam = dnam
		self.sec = sec
		self.event = None
		self.funcs = list()
		self.objs = list()
		self.doc_str = []
		self.doc_order = []

	def set_event(self, nam):
		if self.event != None:
			raise ParseError("Module %s already has $Event",
			    self.nam)
		if not is_c_name(nam):
			raise ParseError("$Event name '%s' is illegal", nam)
		self.event = nam

	def add_func(self, fn):
		self.funcs.append(fn)
		self.doc_order.append(fn)

	def add_obj(self, o):
		self.objs.append(o)
		self.doc_order.append(o)

	def c_proto(self, fo):
		for o in self.objs:
			fo.write("/* Object %s */\n" % o.nam)
			o.fixup(self.nam)
			o.c_proto(fo)
			fo.write("\n")
		if len(self.funcs) > 0:
			fo.write("/* Functions */\n")
		for f in self.funcs:
			for i in lwrap(f.c_proto()):
				fo.write(i + "\n")
		if self.event != None:
			fo.write("\n")
			fo.write("#ifdef VCL_MET_MAX\n")
			fo.write("vmod_event_f " + self.event + ";\n")
			fo.write("#endif\n")

	def c_typedefs_(self):
		l = list()
		for o in self.objs:
			for t in o.c_typedefs(self.nam):
				l.append(t)
			l.append("")
		if len(self.funcs) > 0:
			l.append("/* Functions */")
		for f in self.funcs:
			l.append(f.c_typedef(self.nam))
		l.append("")
		return l

	def c_typedefs(self, fo):
		for i in self.c_typedefs_():
			for j in lwrap(i):
				fo.write(j + "\n")

	def c_vmod(self, fo):

		cs = self.c_struct()
		fo.write(cs + ';\n')

		vfn = 'Vmod_%s_Func' % self.nam

		fo.write("/*lint -esym(754, %s::*) */\n" % vfn)
		fo.write("\nstatic const struct %s Vmod_Func =" % vfn)
		fo.write(self.c_initializer())
		fo.write("\n")

		fo.write("\nstatic const char Vmod_Proto[] =\n")
		for t in self.c_typedefs_():
			for i in lwrap(t, w=64):
				fo.write('\t"' + i + '\\n"\n')
		fo.write('\t"\\n"\n')
		for i in (cs + ";").split("\n"):
			fo.write('\n\t"' + i + '\\n"')
		fo.write('\n\t"static struct ' + vfn + " " + vfn + ';";\n\n')

		fo.write(self.c_strspec())

		fo.write("\n")

		nm = "Vmod_" + self.nam + "_Data"
		fo.write("/*lint -esym(759, %s) */\n" % nm)
		fo.write("const struct vmod_data " + nm + " = {\n")
		fo.write("\t.vrt_major = VRT_MAJOR_VERSION,\n");
		fo.write("\t.vrt_minor = VRT_MINOR_VERSION,\n");
		fo.write("\t.name = \"%s\",\n" % self.nam)
		fo.write("\t.func = &Vmod_Func,\n")
		fo.write("\t.func_len = sizeof(Vmod_Func),\n")
		fo.write("\t.proto = Vmod_Proto,\n")
		fo.write("\t.spec = Vmod_Spec,\n")
		fo.write("\t.abi = VMOD_ABI_Version,\n")

		# NB: Sort of hackish:
		# Fill file_id with random stuff, so we can tell if
		# VCC and VRT_Vmod_Init() dlopens the same file
		#
		fo.write("\t.file_id = \"")
		for i in range(32):
			fo.write("%c" % random.randint(0x40,0x5a))
		fo.write("\",\n")
		fo.write("};\n")

	def c_initializer(self):
		s = '{\n'
		for o in self.objs:
			s += o.c_initializer()

		s += "\n\t/* Functions */\n"
		for f in self.funcs:
			s += f.c_initializer()

		s += "\n\t/* Init/Fini */\n"
		if self.event != None:
			s += "\t" + self.event + ",\n"
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
		if self.event != None:
			s += "\tvmod_event_f\t*_event;\n"
		s += '}'
		return s

	def c_strspec(self):
		s = "/*lint -save -e786 -e840 */\n"
		s += "static const char * const Vmod_Spec[] = {\n"

		for o in self.objs:
			s += o.c_strspec(self.nam) + ",\n\n"

		if len(self.funcs) > 0:
			s += "\t/* Functions */\n"
		for f in self.funcs:
			s += f.c_strspec(self.nam) + ',\n\n'

		if self.event != None:
			s += "\t/* Init/Fini */\n"
			s += '\t"$EVENT\\0Vmod_' + self.nam + '_Func._event",\n'

		s += "\t0\n"
		s += "};\n"
		s += "/*lint -restore */\n"
		return s

	def doc(self, l):
		self.doc_str.append(l)

	def doc_dump(self, fo, suf):
		fo.write(".. role:: ref(emphasis)\n\n")
		i = "vmod_" + self.nam
		fo.write(".. _" + i + "(" + self.sec + "):\n\n")
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

class Func(object):
	def __init__(self, nam, retval, al):
		#if not is_c_name(nam):
		#	raise Exception("Func name '%s' is illegal" % nam)
		if retval not in ctypes:
			raise TypeError(
			    "Return type '%s' not a valid type", retval)
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

	def c_proto(self, fini=False):
		s = ctypes[self.retval] + " vmod_" + self.cnam + "("
		p = ""
		if not fini:
			s += "VRT_CTX"
			p = ", "
		if self.pfx != None:
			s += p + self.pfx
			p = ", "
		for a in self.al:
			s += p + ctypes[a.typ]
			p = ", "
		s += ");"
		return s

	def c_typedef(self, modname, fini=False):
		s = "typedef "
		s += ctypes[self.retval]
		s += " td_" + modname + "_" + self.cnam + "("
		p = ""
		if not fini:
			s += "VRT_CTX"
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
		if len(s.expandtabs()) >= 40:
			s += "\n\t\t\t\t\t"
		else:
			while len(s.expandtabs()) < 40:
				s += "\t"
		s += "*" + self.cnam + ";\n"
		return s

	def c_initializer(self):
		return "\tvmod_" + self.cnam + ",\n"

	def c_strspec(self, modnam, pfx="\t"):
		s = pfx + '"' + modnam + "." + self.nam + '\\0"\n'
		s += pfx + '"'
		s += "Vmod_" + modnam + "_Func." + self.cnam + '\\0"\n'
		s += pfx + '    "' + self.retval + '\\0"\n'
		for a in self.al:
			s += pfx + '\t"' + a.c_strspec() + '"\n'
		s += pfx + '"\\0"'
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

class Obj(object):
	def __init__(self, nam):
		self.nam = nam
		self.init = None
		self.fini = None
		self.methods = list()
		self.doc_str = []
		self.st = None

	def fixup(self, modnam):
		assert self.nam != None
		self.st = "struct vmod_" + modnam + "_" + self.nam
		self.init.set_pfx(self.st + " **, const char *")
		self.fini.set_pfx(self.st + " **")
		for m in self.methods:
			m.set_pfx(self.st + " *")

	def set_init(self, f):
		self.init = f
		self.fini = Func(f.nam, "VOID", [])
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
		l = []
		l += lwrap(self.init.c_proto())
		l += lwrap(self.fini.c_proto(fini=True))
		for m in self.methods:
			l += lwrap(m.c_proto())
		for i in l:
			fo.write(i + "\n")

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
		s += '\t"$OBJ\\0"\n'
		s += self.init.c_strspec(modnam, pfx="\t\t") + '\n'
		s += '\t\t"' + self.st + '\\0"\n'
		s += self.fini.c_strspec(modnam, pfx="\t\t") + '\n'
		for m in self.methods:
			s += m.c_strspec(modnam, pfx="\t\t") + '\n'
		s += '\t"\\0"'
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

class Arg(object):
	def __init__(self, typ, nam=None, det=None):
		self.nam = nam
		self.typ = typ
		self.det = det
		self.val = None

	def __repr__(self):
		return "<ARG %s %s %s>" % (self.nam, self.typ, str(self.det))

	def c_strspec(self):
		if self.det == None:
			s = self.typ + "\\0"
		else:
			s = self.det
		if self.nam != None:
			s += '"\n\t\t    "\\1' + self.nam + '\\0'
		if self.val != None:
			# The space before the value is important to
			# terminate the \2 escape sequence
			s += '"\n\t\t\t"\\2 ' + quote(self.val) + "\\0"
		return s

#######################################################################
#
#
def parse_enum2(tl):
	t = tl.get_token()
	if t.str != "{":
		raise ParseError("expected \"{\"")
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
			raise ParseError(
			    "Expected \"}\" or \",\" not \"%s\"" % t.str)
	s += "\\0"
	return Arg("ENUM", det=s)

def parse_arg(tl, al):
	t = tl.get_token()
	assert t != None

	if t.str == ")":
		return t

	if t.str == "ENUM":
		al.append(parse_enum2(tl))
	elif t.str in ctypes:
		al.append(Arg(t.str))
	else:
		raise Exception("ARG? %s", t.str)

	t = tl.get_token()
	if t.str == "," or t.str == ")":
		return t

	if not is_c_name(t.str):
		raise ParseError(
		    'Expected ")", "," or argument name, not "%s"' % t.str)

	al[-1].nam = t.str
	t = tl.get_token()

	if t.str == "," or t.str == ")":
		return t

	if t.str != "=":
		raise ParseError(
		    'Expected ")", "," or "=", not "%s"' % t.str)

	t = tl.get_token()
	al[-1].val = t.str

	t = tl.get_token()
	return t

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
	return Vmod(nm, dnm, sec)

#######################################################################
#
#

def parse_func(tl, rt_type=None, pobj=None):
	al = list()
	if rt_type == None:
		t = tl.get_token()
		rt_type = t.str
	if rt_type not in ctypes:
		raise TypeError(
		    "Return type '%s' not a valid type" % rt_type)

	t = tl.get_token()
	fname = t.str
	if pobj != None and fname[0] == "." and is_c_name(fname[1:]):
		fname = pobj + fname
	elif not is_c_name(fname):
		raise ParseError("Function name '%s' is illegal", fname)

	t = tl.get_token()
	if t.str != "(":
		raise ParseError("Expected \"(\" got \"%s\"", t.str)

	while True:
		t = parse_arg(tl, al)
		if t.str == ")":
			break
		if t.str != ",":
			raise ParseError("End Of Input looking for ')' or ','")

	f = Func(fname, rt_type, al)

	return f

#######################################################################
#
#

def parse_obj(tl):
	f = parse_func(tl, "VOID")
	o = Obj(f.nam)
	o.set_init(f)
	return o


#######################################################################
# A section of the inputvcc, starting at a keyword

class FileSection(object):
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
		ln, l = self.l.pop(0)
		if l == "":
			return
		l = re.sub("[ \t]*#.*$", "", l)
		l = re.sub("[ \t]*\n", "", l)

		if re.match("['\"]", l):
			m = re.match("(['\"]).*?(\\1)", l)
			if not m:
				raise FormatError("Unbalanced quote",
				    "Unbalanced quote on line %d" % ln)
			self.tl.append(Token(ln, 0, l[:m.end()]))
			self.l.insert(0, (ln, l[m.end():]))
			return

		m = re.search("['\"]", l)
		if m:
			rest = l[m.start():]
			self.l.insert(0, (ln, rest))
			l = l[:m.start()]

		l = re.sub("([(){},=])", r' \1 ', l)
		if l == "":
			return
		for j in l.split():
			self.tl.append(Token(ln, 0, j))

	def parse(self, vx):
		t = self.get_token()
		if t == None:
			return
		t0 = t.str
		if t.str == "$Module":
			o = parse_module(self)
			vx.append(o)
		elif t.str == "$Event":
			x = self.get_token()
			vx[0].set_event(x.str)
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
				raise FormatError("$Method outside $Object", "")
			o = parse_func(self, pobj=vx[1].nam)
			vx[1].add_method(o)
		else:
			raise FormatError("Unknown keyword: %s" % t.str, "")

		assert len(self.tl) == 0
		if o is None and len(self.l) > 0:
			m = "%s description is not included in .rst" % t0
			details = pformat(self.l)
			if opts.strict:
				raise FormatError(m, details)
			else:
				print("WARNING: %s:" % m, file=sys.stderr)
				print(details, file=sys.stderr)
		else:
			for ln, i in self.l:
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


class SimpleTestCase(unittest.TestCase):
	def test_included_vccs(self):
		from tempfile import mktemp
		from glob import glob
		tmpfile = mktemp()
		bdir = dirname(realpath(__file__))
		for inputfile in glob(bdir + "/../libvmod_*/vmod.vcc"):
			runmain(inputfile, outputname=tmpfile)
			for suffix in [".c", ".h"]:
				unlink(tmpfile + suffix)


#######################################################################
def runmain(inputvcc, outputname="vcc_if"):
	# Read the file in
	lines = []
	with open(inputvcc, "r") as fp:
		for i in fp:
			lines.append(i.rstrip())
	ln = 0

	#######################################################################
	# First collect the copyright:  All initial lines starting with '#'

	copy_right = []
	while len(lines[0]) > 0 and lines[0][0] == "#":
		ln += 1
		copy_right.append(lines.pop(0))

	if len(copy_right) > 0:
		if copy_right[0] == "#-":
			copy_right = []
		else:
			while polish(copy_right):
				continue

	if False:
		for i in copy_right:
			print("(C)\t", i)

	#######################################################################
	# Break into sections

	sl = []
	sc = FileSection()
	sl.append(sc)
	while len(lines) > 0:
		ln += 1
		l = lines.pop(0)
		j = l.split()
		if len(j) > 0 and re.match("^\$", j[0]):
			sc = FileSection()
			sl.append(sc)
		sc.add_line(ln, l)

	#######################################################################
	# Parse each section

	try:
		vx = []
		for i in sl:
			i.parse(vx)
			assert len(i.tl) == 0
	except ParseError as e:
		print("ERROR: Parse error reading \"%s\":" % inputvcc)
		pprint(str(e))
		exit(-1)
	except FormatError as e:
		print("ERROR: Format error reading \"%s\": %s" %
		    (inputvcc, pformat(e.msg)))
		print(e.details)
		exit(-2)

	#######################################################################
	# Parsing done, now process
	#

	fc = open("%s.c" % outputname, "w")
	fh = open("%s.h" % outputname, "w")

	write_c_file_warning(fc)
	write_c_file_warning(fh)

	fh.write('struct vmod_priv;\n\n')

	fh.write('extern const struct vmod_data Vmod_%s_Data;\n\n' % vx[0].nam)

	vx[0].c_proto(fh)

	fc.write('#include "config.h"\n')
	fc.write('#include "vcl.h"\n')
	fc.write('#include "vrt.h"\n')
	fc.write('#include "vcc_if.h"\n')
	fc.write('#include "vmod_abi.h"\n')
	fc.write('\n')

	vx[0].c_typedefs(fc)
	vx[0].c_vmod(fc)

	fc.close()
	fh.close()

	for suf in ("", ".man"):
		fp = open("vmod_%s%s.rst" % (vx[0].nam, suf), "w")
		write_rst_file_warning(fp)

		vx[0].doc_dump(fp, suf)

		if len(copy_right) > 0:
			fp.write("\n")
			fp.write("COPYRIGHT\n")
			fp.write("=========\n")
			fp.write("\n::\n\n")
			for i in copy_right:
				fp.write("  %s\n" % i)
			fp.write("\n")


if __name__ == "__main__":
	usagetext = "Usage: %prog [options] <vmod.vcc>"
	oparser = optparse.OptionParser(usage=usagetext)

	oparser.add_option('-N', '--strict', action='store_true', default=False,
	    help="Be strict when parsing input file. (vmod.vcc)")
	oparser.add_option('', '--runtests', action='store_true', default=False,
	    dest="runtests", help=optparse.SUPPRESS_HELP)
	(opts, args) = oparser.parse_args()

	if opts.runtests:
		# Pop off --runtests, pass remaining to unittest.
		del sys.argv[1]
		unittest.main()
		exit()

	i_vcc = None
	if len(args) == 1 and exists(args[0]):
		i_vcc = args[0]
	elif exists("vmod.vcc"):
		if not i_vcc:
			i_vcc = "vmod.vcc"
	else:
		print("ERROR: No vmod.vcc file supplied or found.",
		    file=sys.stderr)
		oparser.print_help()
		exit(-1)

	runmain(i_vcc)
