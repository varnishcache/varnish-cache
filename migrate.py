#!/usr/bin/env python3

COPYRIGHT = '''/*-
 * Copyright (c) 2025 Poul-Henning Kamp
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The stuff extensions should/will need to function
 */
'''

import os
import glob
import subprocess

add_autocrap_files = set()
del_autocrap_files = set()
tests_subdirs = []

class SubDirMakefile():
    def __init__(self, basefile=None):
        self.subdirs = []
        if basefile:
            for line in open(basefile):
                if line[0] != '\t':
                    continue
                self.subdirs.append(line.split()[0])

    def write(self, fn, sort=False):
        if sort:
            self.subdirs.sort()
        with open(fn, "w") as file:
            file.write("#\n")
            file.write("\n")
            file.write("SUBDIRS = \\\n\t")
            file.write(" \\\n\t".join(self.subdirs) + "\n")

def adjust_configure_ac():
    print("-" * 40 + " adjust_configure_ac")

    lines = list(ln for ln in open("configure.ac"))
    conffiles = set()
    idx = lines.index("AC_CONFIG_FILES([\n") + 1
    while lines[idx] != "])\n":
        conffiles.add(lines.pop(idx).strip())
    conffiles |= add_autocrap_files
    conffiles -= del_autocrap_files
    for fn in sorted(conffiles, reverse=True):
        lines.insert(idx, "    " + fn + "\n")
    with open("configure.ac", "w") as file:
        for ln in lines:
            file.write(ln)

def transplant_vtc_file(src, dst):
    subprocess.run(["git", "mv", src, dst], check=True)
    lines = list(i for i in open(dst))
    with open(dst, "w") as file:
        for ln in lines:
            if ln[:9] == "vinyltest" and ln[9:10].isspace():
                ln = "vtest " + ln[10:]
            if ln[:11] == "varnishtest" and ln[11:12].isspace():
                ln = "vtest " + ln[12:]
            if ln[:8] == "varnish ":
                ln = "vinyl " + ln[8:]
            ln = ln.replace("varnishtest", "vinyltest")
            ln = ln.replace("logexpect", "vsl_expect")
            ln = ln.replace("feature user_varnish", "feature user varnish")
            ln = ln.replace("feature group_varnish", "feature group varnish")
            ln = ln.replace("feature user_vcache", "feature user vcache")
            ln = ln.replace("pkg_branch", "vinyl_branch")
            file.write(ln)

def migrate_testcases(dst, *srcs):
    print("-" * 40 + " migrate_testcases", dst)

    tests_subdirs.append(dst)

    dstdir = os.path.join("tests", dst)
    os.makedirs(dstdir)

    tests = []
    for src_pattern in srcs:
        tests += list(glob.glob(src_pattern))
    tests.sort()

    for src in tests:
        transplant_vtc_file(src, os.path.join(dstdir, os.path.basename(src)))

    makefn = os.path.join(dstdir, "Makefile")
    add_autocrap_files.add(makefn)

    with open(makefn + ".am", "w") as file:
        file.write("\n")
        file.write("VTC_LOG_DRIVER = $(top_srcdir)/tools/vtest_driver.sh \\\n")
        file.write("    --top-srcdir ${top_srcdir} --top-builddir ${top_builddir}\n")
        file.write("\n")
        file.write("TEST_EXTENSIONS = .vtc\n")
        file.write("\n")
        file.write("TESTS = \\\n\t")
        file.write(" \\\n\t".join(os.path.basename(x) for x in tests) + "\n")

def write_tests_makefile():
    print("-" * 40 + " write_tests_makefile")

    sd = SubDirMakefile()
    sd.subdirs += tests_subdirs
    sd.write("tests/Makefile.am", sort=False)
    add_autocrap_files.add("tests/Makefile")
      
def add_tests_to_subdirs():
    print("-" * 40 + " add_tests_to_subdirs")

    lines = list(i for i in open("Makefile.am"))
    with open("Makefile.am", "w") as file:
         for ln in lines:
             if "SUBDIRS =" in ln:
                 file.write(ln.rstrip() + " tests\n")
             else:
                 file.write(ln)

def uncouple_varnishtest():
    print("-" * 40 + " uncouple_varnishtest")

    sd = SubDirMakefile("bin/Makefile.am")
    sd.subdirs.remove("varnishtest")
    sd.write("bin/Makefile.am", sort=True)
    del_autocrap_files.add("bin/varnishtest/Makefile")

def add_libvtest_ext_vinyl():
    print("-" * 40 + " add_libvtest_ext_vinyl")

    libdir = "lib/libvtest_ext_vinyl"
    os.makedirs(libdir)
    sd = SubDirMakefile("lib/Makefile.am")
    sd.subdirs.append("libvtest_ext_vinyl")
    sd.write("lib/Makefile.am", sort=True)
    add_autocrap_files.add(libdir + "/Makefile")

    VT="/home/phk/Varnish/VTest2/src"

    for sf, df in (
        ("vtc_varnish.c", "vtc_vinyl.c"),
        ("vtc_vsm.c", "vtc_vsm.c"),
        ("vtc_logexp.c", "vtc_vsl_expect.c"),
    ):
        with open(os.path.join(libdir, df), "w") as file:
             for ln in open(os.path.join(VT,sf)):
                 if "include" in ln and "vtc.h" in ln:
                     ln = ln.replace("vtc.h", "vtest_ext_vinyl.h")
                 ln = ln.replace('"varnish"', '"vinyl"')
                 ln = ln.replace('"logexpect"', '"vsl_expect"')
                 if "params_vsb" in ln:
                     file.write('\tchar *vta = getenv("VINYL_TEST_ARGS");\n');
                     file.write('\tif (vta != NULL) {\n');
                     file.write('\t\tVSB_cat(vsb, " ");\n');
                     file.write('\t\tVSB_cat(vsb, vta);\n');
                     file.write('\t}\n')
                 else:
                     file.write(ln)

    with open(os.path.join(libdir, "vtest_ext_vinyl.h"), "w") as file:
         file.write(COPYRIGHT)
         file.write('''
#include <pthread.h>
#include <signal.h>
#include <limits.h>

#include "vdef.h"

#include "miniobj.h"

#include "vas.h"
#include "vqueue.h"
#include "vsb.h"
#include "vtim.h"

#include "vtest_api.h"

cmd_f cmd_varnish;
cmd_f cmd_logexpect;
cmd_f cmd_vsm;
''')

    with open(os.path.join(libdir, "vtest_ext_vinyl.c"), "w") as file:
         file.write(COPYRIGHT)
         file.write('''
#include "config.h"

#include "vtest_ext_vinyl.h"

static __attribute__((constructor)) void
vtest_ext_vinyl_init(void) {

        add_cmd("vinyl", cmd_varnish, CMDS_F_NONE);
        add_cmd("vsl_expect", cmd_logexpect, CMDS_F_NONE);
        add_cmd("vsm", cmd_vsm, CMDS_F_NONE);

        extmacro_def("vinyl_version", NULL, PACKAGE_VERSION);
        extmacro_def("vinyl_branch", NULL, PACKAGE_BRANCH);
}
''')

    with open(os.path.join(libdir, "Makefile.am"), "w") as file:
         file.write('''
#

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \\
	-I$(top_builddir)/include \\
	@PCRE2_CFLAGS@

AM_CFLAGS   = $(AM_LT_CFLAGS)
AM_LDFLAGS  = $(AM_LT_LDFLAGS)

lib_LTLIBRARIES = libvtest_ext_vinyl.la

libvtest_ext_vinyl_la_LIBADD = \\
	$(top_builddir)/lib/libvarnishapi/libvarnishapi.la

libvtest_ext_vinyl_la_CFLAGS = \\
	-DVARNISH_STATE_DIR='"${VARNISH_STATE_DIR}"' \\
	-DVTEST_WITH_VTC_VSM=1 \\
	-DVTEST_WITH_VTC_VARNISH=1 \\
	-DVTEST_WITH_VTC_LOGEXPECT=1 \\
	$(AM_CFLAGS)

libvtest_ext_vinyl_la_LDFLAGS = $(AM_LDFLAGS)

libvtest_ext_vinyl_la_SOURCES = \\
	vtc_vsl_expect.c \\
	vtc_vinyl.c \\
	vtc_vsm.c \\
	vtest_ext_vinyl.c
''')

def edit_vmod_makefile():
    print("-" * 40 + " edit_vmod_makefile")
    lines = list(i for i in open("vmod/Makefile.am"))
    with open("vmod/Makefile.am", "w") as file:
        for ln in lines:
            if "include" in ln and "/vtc.am" in ln:
                continue
            if "@VMOD_TESTS@" in ln:
                continue
            file.write(ln)

def edit_contrib_makefile():
    print("-" * 40 + " edit_contrib_makefile")
    lines = list(i for i in open("contrib/Makefile.am"))
    with open("contrib/Makefile.am", "w") as file:
        for ln in lines:
            if "include" in ln and "/vtc.am" in ln:
                continue
            if "@CONTRIB_TESTS@" in ln:
                continue
            file.write(ln)

        tests = list(sorted(glob.glob("contrib/tests/*.vtc")))
        file.write("TESTS = \\\n\t")
        file.write(" \\\n\t".join("tests/" + os.path.basename(x) for x in tests) + "\n")

def edit_vtc_am():
    print("-" * 40 + " edit_vtc_am")
    lines = list(i for i in open("vtc.am"))
    with open("vtc.am", "w") as file:
        for ln in lines:
            if "VTC_LOG_COMPILER" in ln:
                file.write("VTC_LOG_COMPILER = $(top_srcdir)/tools/vtest_driver.sh \\\n")
                file.write("\t--top-srcdir ${top_srcdir} --top-builddir ${top_builddir}\n")
            else:
                file.write(ln)

def adapt_testscases():
    print("-" * 40 + " adapt_testscases")

    # Runs varnishtest, but that belongs in Vtest2 now
    subprocess.run(["git", "rm", "bin/varnishtest/tests/r02262.vtc"], check=True)
    subprocess.run(["git", "rm", "bin/varnishtest/tests/u00018.vtc"], check=True)

    # XXX: r03794 needs a "vinyl v1" before server to init the extension
    # XXX: when running vtest out of build environment

def main():

    adapt_testscases()

    # NB: The order here becomes the order tests are run
    for subdir, pattern in (
        ("basic_functionality",   "bin/varnishtest/tests/b*.vtc"),
        ("complex_functionality", "bin/varnishtest/tests/c*.vtc"),
        ("directors",             "bin/varnishtest/tests/d*.vtc"),
        ("edge_side_includes",    "bin/varnishtest/tests/e*.vtc"),
        ("security",              "bin/varnishtest/tests/f*.vtc"),
        ("gzip",                  "bin/varnishtest/tests/g*.vtc"),
        ("haproxy",               "bin/varnishtest/tests/h*.vtc"),
        ("compliance",            "bin/varnishtest/tests/i*.vtc"),
        ("jailing",               "bin/varnishtest/tests/j*.vtc"),
        ("logging",               "bin/varnishtest/tests/l*.vtc"),
        ("infra_vmod",            "bin/varnishtest/tests/m*.vtc"),
        ("proxy_protocol",        "bin/varnishtest/tests/o*.vtc"),
        ("persistence",           "bin/varnishtest/tests/p*.vtc"),
        ("regressions",           "bin/varnishtest/tests/r*.vtc"),
        ("notably_slow_tests",    "bin/varnishtest/tests/s*.vtc"),
        ("http2",                 "bin/varnishtest/tests/t02*.vtc"),
        ("utilities",             "bin/varnishtest/tests/u*.vtc"),
        ("VCL",                   "bin/varnishtest/tests/v*.vtc"),
        ("extensions",            "bin/varnishtest/tests/x*.vtc"),

        #("vmod_blob",             "vmod/tests/blob*.vtc"),
        #("vmod_cookie",           "vmod/tests/cookie*.vtc"),
        #("vmod_directors",        "vmod/tests/directors*.vtc"),
        #("vmod_h2",               "vmod/tests/h2*.vtc"),
        #("vmod_proxy",            "vmod/tests/proxy*.vtc"),
        #("vmod_purge",            "vmod/tests/purge*.vtc"),
        #("vmod_std",              "vmod/tests/std*.vtc"),
        #("vmod_unix",             "vmod/tests/unix*.vtc"),
        #("vmod_vtc",              "vmod/tests/vtc*.vtc"),
    ):
        migrate_testcases(subdir, pattern)

    write_tests_makefile()
    add_tests_to_subdirs()
    add_libvtest_ext_vinyl()
    uncouple_varnishtest()
    #edit_vmod_makefile()
    #edit_contrib_makefile()
    edit_vtc_am()
    adjust_configure_ac()

if __name__ == "__main__":
    main() 
