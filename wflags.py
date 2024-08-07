#!/usr/bin/env python3
#
# Copyright (c) 2021 Varnish Software AS
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

''' Figure out what developer-warnings CC can grok '''

import os
import sys
import subprocess
import tempfile

DESIRABLE_OPTIONS = [
    "-fstack-protector-strong",
    "-Werror",
    "-Wall",
]

DESIRABLE_WFLAGS = [
    "-Wcast-align",
    "-Wcast-qual",
    "-Wchar-subscripts",
    "-Wempty-body",
    "-Wextra",
    "-Wformat -Wformat-y2k",
    "-Wformat -Wformat-zero-length",
    "-Wmissing-field-initializers",
    "-Wmissing-prototypes",
    "-Wnested-externs",
    "-Wpointer-arith",
    "-Wpointer-sign",
    "-Wreturn-type",
    "-Wshadow",
    "-Wstrict-aliasing",
    "-Wstrict-prototypes",
    "-Wstring-plus-int",
    "-Wswitch",
    "-Wunused-parameter",
    "-Wunused-parameters",
    "-Wunused-result",
    "-Wunused-but-set-variable",
    "-Wwrite-strings",
]

UNDESIRABLE_WFLAGS = [
    "-Wno-system-headers", # Outside of our control
    "-Wno-thread-safety", # Does not understand our mutexes are wrapped
    "-Wno-sign-compare", # Fixable
]


def cc(compiler, opt, obj, src):
    a = compiler.split()
    a += ["-c"]
    if opt is not None:
        a += opt.split()
    a += ["-o", obj, src]

    try:
        j = subprocess.check_output(a, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as err:
        if err.output:
            j = err.output
        else:
            j = ("Returncode %d" % err.returncode).encode('utf8')
    return (j)


def main():
    compiler = os.environ.get("CC", "cc")

    src_file = tempfile.NamedTemporaryFile(suffix='.c')
    src_file.write(b'int main(int argc, char **argv) {(void)argc;(void)argv;return(0);}\n')
    src_file.flush()
    obj_file = tempfile.NamedTemporaryFile(suffix='.o')

    j = cc(compiler, None, obj_file.name, src_file.name)
    if j:
        sys.stderr.write(compiler + " failed without flags\n\t" +
                         j.decode('utf8') + '\n')
        sys.exit(1)

    use_flags = []
    for i in DESIRABLE_OPTIONS + DESIRABLE_WFLAGS + UNDESIRABLE_WFLAGS:
        j = cc(compiler, i, obj_file.name, src_file.name)
        if not j:
            use_flags.append(i)
        else:
            sys.stderr.write(compiler + " cannot " + i + '\n')
            if b'error: unrecognized command line option' in j:
                # LLVM
                pass
            elif b'warning: unknown warning option' in j:
                # GCC
                pass
            else:
                sys.stderr.write("\n\t" + j.decode('utf8') + '\n')
    print(" ".join(use_flags))

if __name__ == "__main__":
    main()
