#!/usr/bin/env python3
#
# Copyright (c) 2017 Varnish Software AS
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

"""
This program produces a compact summ of gcov run on all .o files
found in a subdirectory tree.

Options:

    -g gcov-program
        default: "llvm-cov gcov"

    -o output-filename
        default: stdout

    -x exclude-subdir
        default ".git" and ".deps"

 Arguments:

    directories to process.
    default: .

"""

import os
import sys
import getopt
import subprocess

source_files = {}

class SourceFile():
    ''' GCOV counts for a single source file '''

    def __init__(self, path):
        self.path = path
        self.counts = {}
        self.length = 0
        source_files[path] = self

    def __lt__(self, other):
        return self.path < other.path

    def ingest(self, fd):
        ''' Ingest a gcov file '''
        for line in fd:
            flds = line.split(':', maxsplit=2)
            cnt = flds[0].strip()
            if cnt == "-":
                continue
            if cnt == "#####":
                cnt = 0
            else:
                cnt = int(cnt)
            lno = int(flds[1])
            if lno not in self.counts:
                self.counts[lno] = cnt
            else:
                self.counts[lno] += cnt
        self.length = lno

    def summary(self):
        """
        Produce compact output file

        Format:
            linefm [lineto] count

            "+" in linefm means "previous line + 1"
            "." in count means "same as previous count"
        """
        yield "/" + self.path + " " + str(self.length)
        lnos = list(sorted(self.counts.items()))
        pln = -1
        pcn = -1
        while lnos:
            ln, cn = lnos.pop(0)
            lnl = ln
            while lnos:
                lnn, cnn = lnos[0]
                if lnl + 1 != lnn or cnn != cn:
                    break
                lnos.pop(0)
                lnl = lnn
            if ln == pln + 1:
                s = "+ "
            else:
                s = "%d " % ln

            if ln != lnl:
                s += "%d " % lnl
                pln = lnl
            else:
                pln = ln

            if cn == pcn:
                s += "."
            else:
                s += "%d" % cn
                pcn = cn
            yield s

exclude = [".git", ".deps",]

def process_gcov(fn, root):
    """ Sum .gcov file into counts, then delete it """

    with open(fn, encoding="UTF-8") as gfile:
        sline = gfile.readline().split(':')
        assert sline[2] == "Source"
        sname = os.path.normpath(os.path.join(root, sline[3].strip()))
        sfile = source_files.get(sname)
        if not sfile:
            sfile = SourceFile(sname)
        sfile.ingest(gfile)
    os.remove(fn)

def run_gcov(prog, subdir):
    """ Walk tree, run gcov, process output """
    for root, dirs, files in os.walk(subdir):
        for i in exclude:
            if i in dirs:
                dirs.remove(i)
        if " ".join(files).find(".gcda") == -1:
            continue
        for fn in files:
            if fn[-2:] != ".o":
                continue

            # if we find the .o file in a .../.libs the sources
            # must be found relative to the parent directory

            if "varnishd" in root:
                subdir = root.split("/")[-1]
                cmd = ["cd " + root + "/.. && " + "exec " + prog + " " + subdir + "/" + fn]
                rpath = "/../"
            elif "vudp" in fn:
                cmd = ["cd " + root + "/../.. && " + "exec " + prog + " vtest2/lib/" + fn]
                rpath = "/../../"
                #print("VT2.LIB")
            elif "vtest2" in root:
                cmd = ["cd " + root + "/../.. && " + "exec " + prog + " vtest2/src/" + fn]
                rpath = "/../../"
                #print("VT2")
            elif root[-6:] == "/.libs":
                cmd = ["cd " + root + "/.. && " + "exec " + prog + " .libs/" + fn]
                rpath = "/../"
            else:
                #print("R", root, "FN", fn)
                cmd = ["cd " + root + " && " + "exec " + prog + " " + fn]
                rpath = "/"

            sp = subprocess.run(
                    cmd,
                    shell=True,
                    universal_newlines=True,
                    capture_output=True,
            )

            if sp.returncode != 0 or sp.stderr:
                print("CMD", cmd, "EXIT", sp.returncode)
                print("SERR", len(sp.stderr), sp.stderr)
                print("SOUT", sp.stdout)
                sys.exit(2)
            x = sp.stdout

            for ln in x.split("\n"):
                if "such file" in ln:
                    print("LN", ln)
                    assert "such file" not in ln
                ln = ln.split()
                if not ln:
                    continue
                if "reating" in ln[0]:
                    gn = root + rpath + ln[1].strip("'")
                    gn = os.path.normpath(gn)
                    assert gn[-5:] == ".gcov"
                    #print("GS", gn)
                    process_gcov(gn, root.replace("/.libs", ""))

def main():
    ''' ... '''
    optlist, args = getopt.getopt(sys.argv[1:], "g:o:x:")

    fo = sys.stdout
    gcovprog = os.environ.get('GCOVPROG')
    if gcovprog is None:
        gcovprog = "llvm-cov gcov"

    for f, v in optlist:
        if f == '-o' and v == '-':
            fo = sys.stdout
        elif f == '-o':
            fo = open(v, "w", encoding="utf8")
        elif f == '-g':
            gcovprog = v
        elif f == '-x':
            exclude.append(v)
        else:
            assert False
    if not args:
        args = ["."]
    for dn in args:
        run_gcov(gcovprog, dn)

    for sf in sorted(source_files.values()):
        for i in sf.summary():
            fo.write(i + "\n")

if __name__ == "__main__":
    main()
