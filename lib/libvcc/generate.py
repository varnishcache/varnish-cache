#!/usr/bin/env python3
#
# Copyright (c) 2006 Verdens Gang AS
# Copyright (c) 2006-2015 Varnish Software AS
# All rights reserved.
#
# Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
#
# SPDX-License-Identifier: BSD-2-Clause
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

import copy
import sys
from os.path import join

srcroot = "../.."
buildroot = "../.."
if len(sys.argv) == 3:
    srcroot = sys.argv[1]
    buildroot = sys.argv[2]
elif len(sys.argv) != 1:
    print("Two arguments or none")
    exit(2)

tokens = {
    "T_INC":    "++",
    "T_DEC":    "--",
    "T_CAND":       "&&",
    "T_COR":    "||",
    "T_LEQ":    "<=",
    "T_EQ":     "==",
    "T_NEQ":    "!=",
    "T_GEQ":    ">=",
    "T_SHR":    ">>",
    "T_SHL":    "<<",
    "T_INCR":       "+=",
    "T_DECR":       "-=",
    "T_MUL":    "*=",
    "T_DIV":    "/=",
    "T_NOMATCH":    "!~",

    # Single char tokens, for convenience on one line
    None:       "{}()*+-/%><=;!&.|~,",

    # These have handwritten recognizers
    "ID":       None,
    "CNUM":     None,
    "FNUM":     None,
    "CSTR":     None,
    "CSRC":     None,
    "CBLOB":    None,

    # These are intrinsic tokens
    "INC_PUSH": None,
    "INC_POP":  None,

    # End of token list
    "EOI":      None,
}

#######################################################################
# Our methods and actions

returns = (
    ###############################################################
    # Client side

    ('recv',
     "C",
     ('fail', 'synth', 'restart', 'pass', 'pipe', 'hash', 'purge', 'vcl')
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
     ('fail', 'synth', 'restart', 'pass', 'deliver',)
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
     ('fail', 'fetch', 'abandon', 'error')
    ),
    ('backend_response',
     "B",
     ('fail', 'deliver', 'retry', 'abandon', 'pass', 'error')
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

varprotos = {}

def varproto(s):
    if not s in varprotos:
        fh.write(s + ";\n")
        varprotos[s] = True

class vardef(object):
    def __init__(self, nam, typ, rd, wr, wu, vlo, vhi):
        self.nam = nam
        self.typ = typ
        self.rd = rd
        self.wr = wr
        self.uns = wu
        self.vlo = vlo
        self.vhi = vhi

        self.emit()

    def emit(self):
        fh.write("\n")
        fo.write("\n")
        cnam = self.nam.replace(".", "_")
        ctyp = vcltypes[self.typ]

        # fo.write("\t{ \"%s\", %s,\n" % (nm, self.typ))
        fo.write("\tsym = VCC_MkSym(tl, \"%s\", SYM_MAIN," % self.nam)
        if self.typ == "HEADER":
            fo.write(" SYM_NONE, %d, %d);\n" % (self.vlo, self.vhi))
            fo.write("\tAN(sym);\n")
            fo.write("\tsym->wildcard = vcc_Var_Wildcard;\n")
        else:
            fo.write(" SYM_VAR, %d, %d);\n" % (self.vlo, self.vhi))
        fo.write("\tAN(sym);\n")
        fo.write("\tsym->type = %s;\n" % self.typ)
        fo.write("\tsym->eval = vcc_Eval_Var;\n")

        if self.typ == "HEADER":
            fo.write('\tsym->rname = "HDR_')
            fo.write(self.nam.split(".")[0].upper())
            fo.write('";\n')
        elif self.rd:
            fo.write('\tsym->rname = "VRT_r_%s(ctx)";\n' % cnam)
            varproto("VCL_" + self.typ + " VRT_r_%s(VRT_CTX)" % cnam)
        fo.write("\tsym->r_methods =\n")
        restrict(fo, self.rd)
        fo.write(";\n")

        if self.typ == "HEADER":
            fo.write('\tsym->lname = "HDR_')
            fo.write(self.nam.split(".")[0].upper())
            fo.write('";\n')
        elif self.wr:
            fo.write('\tsym->lname = "VRT_l_%s(ctx, ";\n' % cnam)
            s = "void VRT_l_%s(VRT_CTX, " % cnam
            if self.typ == "STRING":
                s += ctyp.c + ", ...)"
            elif self.typ == "BODY":
                s += "enum lbody_e, " + ctyp.c + ", ...)"
            else:
                s += "VCL_" + self.typ + ")"
            varproto(s)
        fo.write("\tsym->w_methods =\n")
        restrict(fo, self.wr)
        fo.write(";\n")

        if self.uns:
            varproto("void VRT_u_%s(VRT_CTX)" % cnam)
            fo.write('\tsym->uname = "VRT_u_%s(ctx)";\n' % cnam)
        fo.write('\tsym->u_methods =\n')
        restrict(fo, self.uns)
        fo.write(";\n")

def parse_vcl(x):
    vlo, vhi = (0, 99)
    x = x.split()
    if x[0] == "VCL" and x[1] == "<=":
        vhi = int(float(x[2]) * 10)
    elif x[0] == "VCL" and x[1] == ">=":
        vlo = int(float(x[2]) * 10)
    else:
        print("Unknown variable version spec")
        print("XXX", x, vlo, vhi)
        exit(2)
    return vlo, vhi

def parse_var(ln):
    l1 = ln.pop(0).split("``")
    assert len(l1) in (1, 3)
    vn = l1[0].strip()
    if vn[-2:] == '.*':
        vn = vn[:-2]
    if len(l1) == 3:
        vlo, vhi = parse_vcl(l1[1])
    else:
        vlo, vhi = 0, 99
    vr = []
    vw = []
    vu = []
    while True:
        l = ln.pop(0)
        if l == "":
            continue
        j = l.split()
        if j[0] == "Type:":
            assert len(j) == 2
            vt = j[1]
            continue
        if j[0] == "Readable" and j[1] == "from:":
            for i in j[2:]:
                vr.append(i.strip(",."))
            continue
        if j[0] == "Writable" and j[1] == "from:":
            for i in j[2:]:
                vw.append(i.strip(",."))
            continue
        if j[0] == "Unsetable" and j[1] == "from:":
            for i in j[2:]:
                vu.append(i.strip(",."))
            continue
        break
    if vn[:8] != "storage.":
        vardef(vn, vt, vr, vw, vu, vlo, vhi)

def parse_var_doc(fn):
    l = []
    for i in open(fn):
        l.append(i.rstrip())
    for n in range(0, len(l)):
        j = l[n].split()
        if len(j) != 2 or j[0] != "Type:" or not l[n][0].isspace():
            continue
        m = n
        while m < len(l) and (l[m] == "" or l[m][0].isspace()):
            m += 1
        parse_var(l[n-2:m-1])

stv_variables = (
    ('free_space', 'BYTES', "0.", 'storage.<name>.free_space', """
    Free space available in the named stevedore. Only available for
    the malloc stevedore.
    """),
    ('used_space', 'BYTES', "0.", 'storage.<name>.used_space', """
    Used space in the named stevedore. Only available for the malloc
    stevedore.
    """),
    ('happy', 'BOOL', "0", 'storage.<name>.happy', """
    Health status for the named stevedore. Not available in any of the
    current stevedores.
    """),
)

#######################################################################
# VCL to C type conversion

vcltypes = {}

class vcltype(object):
    def __init__(self, name, ctype, internal=False):
        self.name = name
        self.c = ctype
        self.internal = internal
        vcltypes[name] = self


vcltype("STRINGS", "void", True)
vcltype("STRING_LIST", "void*", True)
vcltype("SUB", "void*", True)

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
    vcltype(j[-1][4:-1], d)
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
        if i[0] == last_initial:
            continue
        last_initial = i[0]
        fo.write("\tcase '%s':\n" % last_initial)
        need_ret = True
        for j in rrecog:
            if j[0] != last_initial:
                continue
            if len(j) == 2:
                fo.write("\t\tM2('%s', %s);\n" % (j[1], emit[j]))
            elif len(j) == 1:
                fo.write("\t\tM1();\n")
                need_ret = False
            else:
                fo.write("\t\tif (")
                k = 1
                l = len(j)
                while k < l:
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
def emit_strings(fo, name, fc):
    "spit out code that outputs the fc iterable with VSB_cat()"

    w = 66          # Width of lines, after white space prefix
    maxlen = 10240  # Max length of string literal

    x = 0
    l = 0

    fo.write('\tVSB_cat(sb, "/* ---===### %s ###===--- */\\n\\n");\n' % name)
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


def emit_file(fo, fd, bn):
    "Read a C-source file and spit out code that outputs it with VSB_cat()"
    fn = join(fd, bn)

    fi = open(fn)
    fc = fi.read()
    fi.close()

    fo.write("\n\t/* %s */\n\n" % fn)
    emit_strings(fo, bn, fc)

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
#ifdef VCL_H_INCLUDED
#  error "vcl.h included multiple times"
#endif
#define VCL_H_INCLUDED

#ifndef VRT_H_INCLUDED
#  error "include vrt.h before vcl.h"
#endif
""")


def tbl40(a, b):
    while len(a.expandtabs()) < 40:
        a += "\t"
    return a + b

fo.write("\n/* VCL Methods */\n")
task = {}
n = 1
for i in returns:
    fo.write(tbl40("#define VCL_MET_%s" % i[0].upper(), "(1U << %d)\n" % n))
    if not i[1] in task:
        task[i[1]] = []
    task[i[1]].append("VCL_MET_" + i[0].upper())
    n += 1

fo.write("\n" + tbl40("#define VCL_MET_MAX", "%d\n" % n))
fo.write("\n" + tbl40("#define VCL_MET_MASK", "0x%x\n" % ((1 << n) - 1)))

fo.write("\n")
for i in sorted(task.keys()):
    fo.write(tbl40("#define VCL_MET_TASK_%s" % i.upper(),
                   "( " + (" | \\\n\t\t\t\t\t  ").join(task[i]) + " )\n"))

fo.write("\n")
fo.write(tbl40("#define VCL_MET_TASK_ALL", "( VCL_MET_TASK_"))
fo.write(" | \\\n\t\t\t\t\t  VCL_MET_TASK_".join(map(str.upper, task.keys())))
fo.write(" )")

fo.write("\n/* VCL Returns */\n")
n = 1
for i in sorted(rets.keys()):
    fo.write(tbl40("#define VCL_RET_%s" % i.upper(), "%d\n" % n))
    n += 1

fo.write("\n" + tbl40("#define VCL_RET_MAX", "%d\n" % n))

fo.write("\n/* VCL Types */\n")
fo.write('''
struct vrt_type {
\tunsigned\t\tmagic;
#define VRT_TYPE_MAGIC\t\t0xa943bc32
\tconst char\t\t*lname;
\tconst char\t\t*uname;
\tconst char\t\t*ctype;
\tsize_t\t\t\tszof;
};

''')
for vcltype in sorted(vcltypes.keys()):
    fo.write("extern const struct vrt_type VCL_TYPE_%s[1];\n" % vcltype)


fo.write("""
/* Compiled VCL Interface */
typedef int vcl_event_f(VRT_CTX, enum vcl_event_e);
typedef int vcl_init_f(VRT_CTX);
typedef void vcl_fini_f(VRT_CTX);

struct VCL_conf {
	unsigned		magic;
#define VCL_CONF_MAGIC		0x7406c509      /* from /dev/random */

	unsigned		syntax;
	VCL_BACKEND		*default_director;
	VCL_PROBE		default_probe;
	unsigned		nref;
	const struct vpi_ref	*ref;

	int			nsrc;
	unsigned		nsub;
	const char		**srcname;
	const char		**srcbody;

	int			nvmod;
	const struct vpi_ii	*instance_info;

	vcl_event_f		*event_vcl;
""")

for i in returns:
    fo.write("\tvcl_func_f\t\t*" + i[0] + "_func;\n")

fo.write("\n};\n")
fo.close()

#######################################################################


def restrict(fo, spec):
    d = dict()
    for j in spec:
        if j[:4] == "vcl_":
            j = j[4:]
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
            if not j in vcls:
                print("JJ", j)
            assert j in vcls
            d[j] = True
    p = ""
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
    if not d:
        fo.write("0")

#######################################################################

fh = open(join(buildroot, "include/vrt_obj.h"), "w")
file_header(fh)

fo = open(join(buildroot, "lib/libvcc/vcc_obj.c"), "w")
file_header(fo)

fo.write("""
#include "config.h"

#include "vcc_compile.h"

void
vcc_Var_Init(struct vcc *tl)
{
    struct symbol *sym;
""")

parse_var_doc(join(srcroot, "doc/sphinx/reference/vcl_var.rst"))
fo.write("}\n")

for i in stv_variables:
    fh.write("VCL_" + i[1] + " VRT_stevedore_" + i[0] + "(VCL_STEVEDORE);\n")

fo.write("\n/* VCL type identifiers */\n")

for vn in sorted(vcltypes.keys()):
    v = vcltypes[vn]
    if v.internal:
        continue
    fo.write("const struct vrt_type VCL_TYPE_%s[1] = { {\n" % v.name)
    fo.write("\t.magic = VRT_TYPE_MAGIC,\n")
    fo.write('\t.lname = "%s",\n' % v.name.lower())
    fo.write('\t.uname = "%s",\n' % v.name)
    fo.write('\t.ctype = "%s",\n' % v.c)
    if v.c != "void":
        fo.write('\t.szof = sizeof(VCL_%s),\n' % v.name)
    fo.write("}};\n")

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
emit_file(fo, srcroot, "include/vrt.h")
emit_file(fo, buildroot, "include/vcl.h")
emit_file(fo, buildroot, "include/vrt_obj.h")
emit_file(fo, srcroot, "include/vcc_interface.h")
emit_strings(fo, "vgc asserts (generate.py)",
'''#define assert(e)							\\
do {									\\
	if (!(e)) {							\\
		VPI_Fail(__func__, __FILE__, __LINE__, #e);		\\
	}								\\
} while (0)
''')

fo.write("\n}\n")
fo.close()

#######################################################################
ft = open(join(buildroot, "lib/libvcc/vcc_types.h"), "w")
file_header(ft)

lint_start(ft)

for vcltype in sorted(vcltypes.keys()):
    ft.write("VCC_TYPE(" + vcltype + ", " + vcltype.lower() +")\n")
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
    fo.write(ct.c + ",\t" + i[2] + ")")
    fo.write("\n")

fo.write("#undef VRTSTVVAR\n")
lint_end(fo)
fo.close()
