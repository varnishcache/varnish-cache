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
    "-Wformat-y2k",
    "-Wformat-zero-length",
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
    "-Wsystem-headers",
    "-Wunused-parameter",
    "-Wunused-parameters",
    "-Wunused-result",
    "-Wwrite-strings",
]

UNDESIRABLE_WFLAGS = [
    "-Wno-thread-safety", # Does not understand our mutexs are wrapped
    "-Wno-old-style-definition", # Does not like vgz
    "-Wno-sign-compare", # Fixable
    "-Wno-implicit-fallthrough", # Probably Fixable
    "-Wno-builtin-requires-header", # Complains about linux::pthread.h
    "-Wno-incomplete-setjmp-declaration", # Clang complains about glibc pthread.h
    "-Wno-redundant-decls", # Complains about centos::stdio.h
    "-Wno-missing-variable-declarations", # Complains about optreset
]

def main():
    compiler = os.environ.get("CC", "cc")

    src_file = tempfile.NamedTemporaryFile(suffix='.c')
    src_file.write(b'int main(int argc, char **argv) {(void)argc;(void)argv;return(0);}\n')
    src_file.flush()
    obj_file = tempfile.NamedTemporaryFile(suffix='.o')

    use_flags = []
    for i in DESIRABLE_OPTIONS + DESIRABLE_WFLAGS + UNDESIRABLE_WFLAGS:
        j = subprocess.run(
            [
                compiler,
                "-c",
                i,
                "-o", obj_file.name,
                src_file.name,
            ],
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE,
        )
        if not j.returncode and not j.stdout and not j.stderr:
            use_flags.append(i)
        else:
            sys.stderr.write(compiler + " cannot " + i + '\n')
            if b'error: unrecognized command line option' in j.stderr:
                # LLVM
                pass
            elif b'warning: unknown warning option' in j.stderr:
                # GCC
                pass
            else:
                sys.stderr.write("\n\t" + j.stderr.decode('utf8') + '\n')
    print(" ".join(use_flags))

if __name__ == "__main__":
    main()
