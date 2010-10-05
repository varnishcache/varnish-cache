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
#
# $Id$

import sys

if len(sys.argv) == 2:
	specfile = sys.argv[1]
else:
	specfile = "vmod.vcc"

type_tab = dict()

ctypes = {
	'IP':		"struct sockaddr_storage *",
	'STRING':	"const char *",
	'STRING_LIST':	"const char *, ...",
	'BOOL':		"unsigned",
	'BACKEND':	"struct director *",
	'TIME':		"double",
	'REAL':		"double",
	'DURATION':	"double",
	'INT':		"int",
	'HEADER':	"const char *",
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

def do_func(fname, rval, args):
	global pstruct
	global pinit
	global plist
	global slist
	global tdl
	#print(fname, rval, args)

	proto = ctypes[rval] + " vmod_" + fname + "(struct sess *"
	sproto = ctypes[rval] + " td_" + modname + "_" + fname + "(struct sess *"
	s=", "
	for i in args:
		proto += s + ctypes[i]
		sproto += s + ctypes[i]
	proto += ")"
	sproto += ")"

	plist += proto + ";\n"
	tdl += "typedef " + sproto + ";\n"

	pstruct += "\ttd_" + modname + "_" + fname + "\t*" + fname + ";\n"
	pinit += "\tvmod_" + fname + ",\n"

	s = modname + '.' + fname + "\\0"
	s += "Vmod_Func_" + modname + "." + fname + "\\0"
	s += rval
	for i in args:
		s += '\\0' + i
	slist += '\t"' + s + '\\0",\n'

#######################################################################

def partition(string, separator):
	if (hasattr(string,"partition")):
		return string.partition(separator)
	i = string.find(separator)
	if i >= 0:
		return (string[:i],separator,string[i+len(separator):])
	return (string, '', '')

f = open(specfile, "r")

for l0 in f:
	# print("# " + l0)
	l0=l0.strip()
	if l0 == "":
		continue
	i = l0.find("#")
	if i == 0:
		continue
	if i >= 0:
		line = l0[:i-1]
	else:
		line = l0
	line = line.expandtabs().strip()
	l = partition(line, " ")

	if l[0] == "Module":
		modname = l[2].strip();
		continue

	if l[0] == "Init":
		initname = l[2].strip();
		continue

	if l[0] != "Function":
		assert False

	l = partition(l[2].strip(), " ")
	rt_type = l[0]

	l = partition(l[2].strip(), "(")
	fname = l[0].strip()

	args = list()

	while True:
		l = partition(l[2].strip(), ",")
		if len(l[2]) == 0:
			break
		args.append(l[0])
	l = partition(l[0].strip(), ")")
	args.append(l[0])
	do_func(fname, rt_type, args)

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


fc.write('#include "vcc_if.h"\n')
fc.write('#include "vrt.h"\n')
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

fc.write('const char *Vmod_Spec[] = {\n' + slist + '\t0\n};\n')

fc.write("\n")

