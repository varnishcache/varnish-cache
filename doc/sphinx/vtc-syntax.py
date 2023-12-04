#!/usr/bin/env python3
#
# Copyright (c) 2006-2016 Varnish Software AS
# All rights reserved.
#
# Author: Guillaume Quintard <guillaume.quintard@gmail.com>
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
# Process various varnishtest C files and output reStructuredText to be
# included in vtc(7).

import sys
import re


def parse_file(fn, cl, tl, sl):
    p = False
    section = ""
    resec = re.compile(r"\s*/?\* SECTION: ")

    try:
        # Python3
        f = open(fn, "r", encoding="UTF-8")
    except TypeError:
        # Python2
        f = open(fn, "r")

    for l in f:
        if "*/" in l:
            p = 0
        if resec.match(l):
            a = l.split()
            section = a[2]
            sl.append(section)
            cl[section] = []
            if len(a) > 3:
                tl[section] = re.sub(
                    r"^\s*/?\* SECTION: [^ ]+ +",
                    "", l)
            else:
                tl[section] = ""
            p = 1
        elif p:
            cl[section].append(re.sub(r"^\s*\* ?", "", l))
    f.close()

if __name__ == "__main__":
    cl = {}
    tl = {}
    sl = []
    for fn in sys.argv[1:]:
        parse_file(fn, cl, tl, sl)
    sl.sort()
    for section in sl:
        print(".. _vtc-" + section + ":\n")
        print(tl[section], end="")
        a = section
        c = section.count(".")
        if c == 0:
            r = "-"
        elif c == 1:
            r = "~"
        elif c == 2:
            r = "."
        else:
            r = "*"
        print(re.sub(r".", r, tl[section]), end="")
        print("".join(cl[section]))
