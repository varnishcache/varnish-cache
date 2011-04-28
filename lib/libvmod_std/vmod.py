#!/usr/local/bin/python
#-
# Copyright (c) 2010 Linpro AS
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
	'IP':		"struct sockaddr_storage *",
	'STRING':	"const char *",
	'STRING_LIST':	"const char *, ...",
	'BOOL':		"unsigned",
	'BACKEND':	"struct director *",
	'ENUM':		"const char *",
	'TIME':		"double",
	'REAL':		"double",
	'DURATION':	"double",
	'INT':		"int",
	'HEADER':	"enum gethdr_e, const char *",
	'PRIV_VCL':	"struct vmod_priv *",
	'PRIV_CALL':	"struct vmod_priv *",
	'VOID':		"void",
}

#######################################################################

initname = ""
modname = "???"
pstruct = ""
pinit = ""
tdl = ""
plist = ""
slist = ""

def do_func(fname, rval, args, vargs):
	global pstruct
	global pinit
	global plist
	global slist
	global tdl
	#print(fname, rval, args)

	# C argument list
	cargs = "(struct sess *"
	for i in args:
		cargs += ", " + i
	cargs += ")"

	# Prototypes for vmod implementation and interface typedef
	proto = ctypes[rval] + " vmod_" + fname + cargs
	sproto = ctypes[rval] + " td_" + modname + "_" + fname + cargs

	# append to lists of prototypes
	plist += proto + ";\n"
	tdl += "typedef " + sproto + ";\n"

	# Append to struct members
	pstruct += "\ttd_" + modname + "_" + fname + "\t*" + fname + ";\n"

	# Append to struct initializer
	pinit += "\tvmod_" + fname + ",\n"

	# Compose the vmod spec-string
	s = modname + '.' + fname + "\\0"
	s += "Vmod_Func_" + modname + "." + fname + "\\0"
	s += rval + '\\0'
	for i in vargs:
		s +=  i + '\\0'
	slist += '\t"' + s + '",\n'

#######################################################################

def partition(string, separator):
	if (hasattr(string,"partition")):
		return string.partition(separator)
	i = string.find(separator)
	if i >= 0:
		return (string[:i],separator,string[i+len(separator):])
	return (string, '', '')

#######################################################################

def is_c_name(s):
	return None != re.match("^[a-z][a-z0-9_]*$", s)

#######################################################################

def parse_enum(tq):
	assert tq[0] == '{'
	assert tq[-1] == '}'
	f = tq[1:-1].split(',')
	s="ENUM\\0"
	b=dict()
	for i in f:
		i = i.strip()
		if not is_c_name(i):
			raise Exception("Enum value '%s' is illegal" % i)
		if i in b:
			raise Exception("Duplicate Enum value '%s'" % i)
		b[i] = True
		s = s + i.strip() + '\\0'
	return s

#######################################################################

f = open(specfile, "r")

def nextline():
	while True:
		l0 = f.readline()
		if l0 == "":
			return l0
		l0 = re.sub("#.*$", "", l0)
		l0 = re.sub("\s\s*", " ", l0.strip())
		if l0 != "":
			return l0

while True:
	l0 = nextline()
	if l0 == "":
		break;
	l = partition(l0, " ")

	if l[0] == "Module":
		modname = l[2].strip();
		if not is_c_name(modname):
			raise Exception("Module name '%s' is illegal" % modname)
		continue

	if l[0] == "Init":
		initname = l[2].strip();
		if not is_c_name(initname):
			raise Exception("Init name '%s' is illegal" % initname)
		continue

	if l[0] != "Function":
		raise Exception("Expected 'Function' line, got '%s'" % l[0])

	# Find the return type of the function
	l = partition(l[2].strip(), " ")
	rt_type = l[0]
	if rt_type not in ctypes:
		raise Exception("Return type '%s' not a valid type" % rt_type)

	# Find the function name
	l = partition(l[2].strip(), "(")

	fname = l[0].strip()
	if not is_c_name(fname):
		raise Exception("Function name '%s' is illegal" % fname)

	if l[1] != '(':
		raise Exception("Missing '('")

	l = l[2]

	while -1 == l.find(")"):
		l1 = nextline()
		if l1 == "":	
			raise Exception("End Of Input looking for ')'")
		l = l + l1

	if -1 != l.find("("):
		raise Exception("Nesting trouble with '(...)' ")

	if l[-1:] != ')':
		raise Exception("Junk after ')'")

	l = l[:-1]

	args = list()
	vargs = list()

	for i in re.finditer("([A-Z_]+)\s*({[^}]+})?(,|$)", l):
		at = i.group(1)
		tq = i.group(2)
		if at not in ctypes:
			raise Exception(
			    "Argument type '%s' not a valid type" % at)

		args.append(ctypes[at])

		if at == "ENUM":
			if tq == None:
				raise Exception(
				    "Argument type '%s' needs qualifier {...}" % at)
			at=parse_enum(tq)

		elif tq != None:
			raise Exception(
			    "Argument type '%s' cannot be qualified with {...}" % at)
		
		vargs.append(at)

	do_func(fname, rt_type, args, vargs)

#######################################################################
def dumps(s):
	while True:
		l = partition(s, "\n")
		if len(l[0]) == 0:
			break
		fc.write('\t"' + l[0] + '\\n"\n')
		s = l[2]

#######################################################################

if initname != "":
	plist += "int " + initname + "(struct vmod_priv *, const struct VCL_conf *);\n"
	pstruct += "\tvmod_init_f\t*_init;\n"
	pinit += "\t" + initname + ",\n"
	slist += '\t"INIT\\0Vmod_Func_' + modname + '._init",\n'

#######################################################################

def file_header(fo):
        fo.write("""/*
 *
 * NB:  This file is machine generated, DO NOT EDIT!
 *
 * Edit vmod.vcc and run vmod.py instead
 */

""")

#######################################################################

fc = open("vcc_if.c", "w")
fh = open("vcc_if.h", "w")

file_header(fc)
file_header(fh)

fh.write('struct sess;\n')
fh.write('struct VCL_conf;\n')
fh.write('struct vmod_priv;\n')
fh.write("\n");

fh.write(plist)


fc.write('#include "vrt.h"\n')
fc.write('#include "vcc_if.h"\n')
fc.write("\n");

fc.write("\n");

fc.write(tdl);
fc.write("\n");

fc.write('const char Vmod_Name[] = "' + modname + '";\n')

fc.write("const struct Vmod_Func_" + modname + " {\n")
fc.write(pstruct + "} Vmod_Func = {\n" + pinit + "};\n")
fc.write("\n");

fc.write("const int Vmod_Len = sizeof(Vmod_Func);\n")
fc.write("\n");

fc.write('const char Vmod_Proto[] =\n')
dumps(tdl);
fc.write('\t"\\n"\n')
dumps("struct Vmod_Func_" + modname + " {\n")
dumps(pstruct + "} Vmod_Func_" + modname + ";\n")
fc.write('\t;\n')
fc.write("\n");

fc.write('const char * const Vmod_Spec[] = {\n' + slist + '\t0\n};\n')

fc.write("\n")

