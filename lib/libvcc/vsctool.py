#!/usr/bin/env python3
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

'''
This program compiles a `.vsc` file to `.c`, `.h` and `.rst` formats.

Note: A `.vsc` file is *almost* a `.rst` file, or at last *almost*
the same general syntax as a `.rst` file, but for now we process
it with this program to get a *real* `.rst` file.
'''

from __future__ import print_function

import getopt
import json
import sys
import collections
import codecs

# Parameters of 'varnish_vsc_begin', first element is default
TYPES = ["counter", "gauge", "bitmap"]
CTYPES = ["uint64_t"]
LEVELS = ["info", "diag", "debug"]
FORMATS = ["integer", "bytes", "bitmap", "duration"]

PARAMS = {
    "type": TYPES,
    "ctype": CTYPES,
    "level": LEVELS,
    "oneliner": None,
    "group": None,
    "format": FORMATS,
}

def genhdr(fo, name):

    '''Emit .[ch] file boiler-plate warning'''

    fo.write('/*\n')
    fo.write(' * NB:  This file is machine generated, DO NOT EDIT!\n')
    fo.write(' *\n')
    fo.write(' * Edit ' + name +
             '.vsc and run lib/libvcc/vsctool.py instead.\n')
    fo.write(' */\n')
    fo.write('\n')

#######################################################################

class CounterSet(object):

    '''
        A set of counters

        In the `.vsc` file a CounterSet is everything between a

            .. varnish_vsc_begin::

        and the subsequent

            .. varnish_vsc_end::
    '''

    def __init__(self, name, m):
        self.name = name
        self.struct = "struct VSC_" + name
        self.mbrs = []
        self.groups = {}
        self.head = m
        self.completed = False
        self.off = 0
        self.gnames = None

    def addmbr(self, m, g):
        '''Add a counter'''
        assert not self.completed
        self.mbrs.append(m)
        retval = self.off
        self.off += 8
        if g is not None:
            if g not in self.groups:
                self.groups[g] = []
            self.groups[g].append(m)
        return retval

    def complete(self, arg):
        '''Mark set completed'''
        assert arg == self.name
        self.completed = True
        self.gnames = list(self.groups.keys())
        self.gnames.sort()


    def emit_json(self, fo):
        '''Emit JSON as compact C byte-array and as readable C-comments'''
        assert self.completed
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
        s = json.dumps(dd, separators=(",", ":")) + "\0"
        fo.write("\nstatic const unsigned char")
        fo.write(" vsc_%s_json[%d] = {\n" % (self.name, len(s)))
        bz = bytearray(s, encoding="ascii")
        t = "\t"
        for i in bz:
            t += "%d," % i
            if len(t) >= 69:
                fo.write(t + "\n")
                t = "\t"
        if len(t) > 1:
            fo.write(t[:-1])
        fo.write("\n};\n")
        s = json.dumps(dd, indent=2, separators=(',', ': '))
        fo.write("\n")
        for i in s.split("\n"):
            j = "// " + i
            if len(j) > 72:
                fo.write(j[:72] + "[...]\n")
            else:
                fo.write(j + "\n")
        fo.write("\n")

    def emit_h(self):
        '''Emit .h file'''
        assert self.completed

        fon = "VSC_" + self.name + ".h"
        try:
            # Python3
            fo = open(fon, "w", encoding="UTF-8")
        except TypeError:
            # Python2
            fo = open(fon, "w")
        genhdr(fo, self.name)

        fo.write(self.struct + " {\n")
        for i in self.mbrs:
            s = "\tuint64_t\t%s;" % i.arg
            g = i.param.get("group")
            if g is not None:
                while len(s.expandtabs()) < 64:
                    s += "\t"
                s += "/* %s */" % g
            fo.write(s + "\n")
        fo.write("};\n")
        fo.write("\n")

        for i in self.gnames:
            fo.write(self.struct + "_" + i + " {\n")
            for j in self.groups[i]:
                fo.write("\tuint64_t\t%s;\n" % j.arg)
            fo.write("};\n")
            fo.write("\n")

        fo.write("#define VSC_" + self.name +
                 "_size PRNDUP(sizeof(" + self.struct + "))\n\n")

        fo.write(self.struct + " *VSC_" + self.name + "_New")
        fo.write("(struct vsmw_cluster *,\n")
        fo.write("    struct vsc_seg **, const char *fmt, ...);\n")

        fo.write("void VSC_" + self.name + "_Destroy")
        fo.write("(struct vsc_seg **);\n")

        sf = self.head.param.get('sumfunction')
        if sf is not None:
            for i in sf.split():
                j = i.split("_")
                assert len(j) <= 2
                if len(j) == 1:
                    fo.write("void VSC_" + self.name + "_Summ_" + i)
                    fo.write("(" + self.struct + " *, ")
                    fo.write("const " + self.struct + "_" + i + " *);\n")
                else:
                    fo.write("void VSC_" + self.name + "_Summ_" + i)
                    fo.write("(" + self.struct + "_" + j[0] + " *, ")
                    fo.write("const " + self.struct + "_" + j[1] + " *);\n")

    def emit_c_paranoia(self, fo):
        '''Emit asserts to make sure compiler gets same byte index'''
        fo.write("#define PARANOIA(a,n)\t\t\t\t\\\n")
        fo.write("    _Static_assert(\t\t\t\t\\\n")
        fo.write("\toffsetof(" + self.struct + ", a) == n,\t\\\n")
        fo.write("\t\"VSC element '\" #a \"' at wrong offset\")\n\n")

        for i in self.mbrs:
            fo.write("PARANOIA(" + i.arg)
            fo.write(", %d);\n" % (i.param["index"]))

        fo.write("#undef PARANOIA\n")

    def emit_c_sumfunc(self, fo, tgt):
        '''Emit a function summ up countersets'''
        fo.write("\n")
        fo.write("void\n")
        fo.write("VSC_" + self.name + "_Summ")
        fo.write("_" + tgt[0])
        if len(tgt) > 1:
            fo.write("_" + tgt[1])
            fo.write("(" + self.struct + "_" + tgt[1])
        else:
            fo.write("(" + self.struct)
        fo.write(" *dst, const " + self.struct + "_" + tgt[0] + " *src)\n")
        fo.write("{\n")
        fo.write("\n")
        fo.write("\tAN(dst);\n")
        fo.write("\tAN(src);\n")
        for i in self.groups[tgt[0]]:
            s1 = "\tdst->" + i.arg + " +="
            s2 = "src->" + i.arg + ";"
            if len((s1 + " " + s2).expandtabs()) < 79:
                fo.write(s1 + " " + s2 + "\n")
            else:
                fo.write(s1 + "\n\t    " + s2 + "\n")
        fo.write("}\n")

    def emit_c_newfunc(self, fo):
        '''Emit New function'''
        fo.write("\n")
        fo.write(self.struct + "*\n")
        fo.write("VSC_" + self.name + "_New")
        fo.write("(struct vsmw_cluster *vc,\n")
        fo.write("    struct vsc_seg **sg, const char *fmt, ...)\n")
        fo.write("{\n")
        fo.write("\tva_list ap;\n")
        fo.write("\t" + self.struct + " *retval;\n")
        fo.write("\n")
        fo.write("\tva_start(ap, fmt);\n")
        fo.write("\tretval = VRT_VSC_Alloc")
        fo.write("(vc, sg, vsc_" + self.name + "_name, ")
        fo.write("VSC_" + self.name + "_size,\n")
        fo.write("\t    vsc_" + self.name + "_json, ")
        fo.write("sizeof vsc_" + self.name + "_json, fmt, ap);\n")
        fo.write("\tva_end(ap);\n")
        fo.write("\treturn(retval);\n")
        fo.write("}\n")

    def emit_c_destroyfunc(self, fo):
        '''Emit Destroy function'''
        fo.write("\n")
        fo.write("void\n")
        fo.write("VSC_" + self.name + "_Destroy")
        fo.write("(struct vsc_seg **sg)\n")
        fo.write("{\n")
        fo.write("\tstruct vsc_seg *p;\n")
        fo.write("\n")
        fo.write("\tAN(sg);\n")
        fo.write("\tp = *sg;\n")
        fo.write("\t*sg = NULL;\n")
        fo.write('\tVRT_VSC_Destroy(vsc_%s_name, p);\n' % self.name)
        fo.write("}\n")

    def emit_c(self):
        '''Emit .c file'''
        assert self.completed
        fon = "VSC_" + self.name + ".c"
        try:
            # Python3
            fo = open(fon, "w", encoding="UTF-8")
        except TypeError:
            # Python2
            fo = open(fon, "w")
        genhdr(fo, self.name)
        fo.write('#include "config.h"\n')
        fo.write('#include <stdio.h>\n')
        fo.write('#include <stdarg.h>\n')
        fo.write('#include "vdef.h"\n')
        fo.write('#include "vas.h"\n')
        fo.write('#include "vrt.h"\n')
        fo.write('#include "VSC_%s.h"\n' % self.name)

        fo.write("\n")
        fo.write('static const char vsc_%s_name[] = "%s";\n' %
                 (self.name, self.name.upper()))

        fo.write("\n")

        self.emit_c_paranoia(fo)
        self.emit_json(fo)
        self.emit_c_newfunc(fo)
        self.emit_c_destroyfunc(fo)
        sf = self.head.param.get('sumfunction')
        if sf is not None:
            for i in sf.split():
                self.emit_c_sumfunc(fo, i.split("_"))

#######################################################################

class OurDirective(object):

    '''
        One of our `.. blablabla::` directives in the source file
    '''

    def __init__(self, s):
        ll = s.split("\n")
        i = ll.pop(0).split("::", 2)
        self.cmd = i[0]
        self.arg = i[1].strip()
        assert len(self.arg.split()) == 1

        self.param = {}
        while ll:
            j = ll[0].split(":", 2)
            if len(j) != 3 or not j[0].isspace():
                break
            self.param[j[1]] = j[2].strip()
            ll.pop(0)
        self.ldoc = ll

    def getdoc(self):
        '''
        Get docs for JSON

        Note that these docs end with the first '\n.. ' sequence
        in the .vsc file, so that we can put a longer and more
        complex description into the .RST docs than the "long"
        explanation varnishstat(1) and similar programs provide.
        '''
        while self.ldoc and self.ldoc[0].strip() == "":
            self.ldoc.pop(0)
        while self.ldoc and self.ldoc[-1].strip() == "":
            self.ldoc.pop(-1)
        return self.ldoc

    def emit_rst(self, fo):
        '''Emit the documentation as .rst'''
        assert False

class RstVscDirectiveBegin(OurDirective):

    '''
        `varnish_vsc_begin` directive
    '''

    def __init__(self, s, vsc_set, fo):
        super(RstVscDirectiveBegin, self).__init__(s)
        vsc_set.append(CounterSet(self.arg, self))
        if fo:
            fo.write("\n..\n\t" + self.cmd + ":: " + self.arg + "\n")

            s = self.arg.upper() + " – " + self.param["oneliner"]
            fo.write("\n")
            fo.write(s + "\n")
            fo.write("=" * len(s) + "\n")
            fo.write("\n".join(self.ldoc))

class RstVscDirective(OurDirective):

    '''
        `varnish_vsc` directive - one counter
    '''

    def __init__(self, s, vsc_set, fo):
        assert not vsc_set or vsc_set[-1].complete
        super(RstVscDirective, self).__init__(s)

        for i, v in PARAMS.items():
            if v is not None:
                if i not in self.param:
                    self.param[i] = v[0]
                if self.param[i] not in v:
                    sys.stderr.write("Wrong " + i + " '" + self.param[i])
                    sys.stderr.write("' on field '" + self.arg + "'\n")
                    exit(2)

        for p in self.param:
            if p in PARAMS:
                continue
            sys.stderr.write("Unknown parameter ")
            sys.stderr.write("'" + p + "'")
            sys.stderr.write(" on field '" + self.arg + "'\n")
            exit(2)
        self.param["index"] = vsc_set[-1].addmbr(self, self.param.get("group"))
        if fo:
            fo.write("\n``%s`` – " % self.arg)
            fo.write("`%s` - " % self.param["type"])
            fo.write("%s\n\n" % self.param["level"])

            fo.write("\t" + self.param["oneliner"] + "\n")
            fo.write("\n".join(self.ldoc))

class RstVscDirectiveEnd(OurDirective):

    '''
        `varnish_vsc_end` directive
    '''

    def __init__(self, s, vsc_set, fo):
        super(RstVscDirectiveEnd, self).__init__(s)
        vsc_set[-1].complete(self.arg)
        if fo:
            fo.write("\n..\n\t" + self.cmd + ":: " + self.arg + "\n")
            fo.write("\n".join(self.ldoc))

#######################################################################

def mainfunc(argv):

    '''Process a .vsc file'''

    optlist, args = getopt.getopt(argv[1:], "chr")

    if len(args) != 1:
        sys.stderr.write("Need exactly one filename argument\n")
        exit(2)

    rstfile = None
    for f, v in optlist:
        if f == '-r':
            try:
                # Python3
                sys.stdout = codecs.getwriter("utf-8")(sys.stdout.detach())
            except AttributeError:
                # Python2
                pass
            rstfile = sys.stdout

    vscset = []
    try:
        # Python3
        f = open(args[0], encoding="UTF-8")
    except TypeError:
        # Python2
        f = open(args[0])
    scs = f.read().split("\n.. ")
    if rstfile:
        rstfile.write(scs[0])
    for i in scs[1:]:
        j = i.split(None, 1)
        f = {
            "varnish_vsc_begin::":  RstVscDirectiveBegin,
            "varnish_vsc::":        RstVscDirective,
            "varnish_vsc_end::":    RstVscDirectiveEnd,
        }.get(j[0])
        if f is not None:
            f(i, vscset, rstfile)
        elif rstfile:
            rstfile.write("\n.. " + i)

    for i in vscset:
        for f, v in optlist:
            if f == '-h':
                i.emit_h()
            if f == '-c':
                i.emit_c()

if __name__ == "__main__":

    mainfunc(sys.argv)
