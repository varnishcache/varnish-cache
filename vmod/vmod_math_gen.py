#!/usr/bin/env python3

# Copyright 2025 UPLEX - Nils Goroll Systemoptimierung
# All rights reserved.
#
# Author: Nils Goroll <nils.goroll@uplex.de>
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
# generate vmod_math vcc and c files from opengroup spec html

import re
import sys


vcc_type_map = {
    "int": "INT",
    "double": "REAL",
    "long": "INT",
    "real-floating": "REAL",
    "const char *": "STRING",
}

def_arg_names = ["x", "y", "z"]
arg_names_map = {
    "atan2": ["y", "x"],
    "ldexp": ["x", "e"],
    "nan": ["tag"],
    "scalbln": ["x", "e"],
    "yn": ["n", "x"],
}

# type name
re_arg = re.compile(r"^(.+?)(\s+[xy])?$")
# argument separator
re_argsep = re.compile(r",\s*")


def vcc_arg(arg, n):
    match = re_arg.search(arg)
    if match is None:
        print(f"Error: no re_arg match on '{arg}'")
        sys.exit(1)
    # print(match)

    atype = vcc_type_map[match.group(1)]
    name = match.group(2)

    if name is None:
        name = n

    return f"{atype} {name}"


def gen_func_vcc(match, vcc_fh):
    rtype = match.group(1)
    name = match.group(2)
    args = match.group(3)

    # print(match)

    rtype = vcc_type_map[rtype]
    args = re_argsep.split(args)

    argnames = def_arg_names[: len(args)]
    if name in arg_names_map:
        argnames = arg_names_map[name]

    args = ", ".join([vcc_arg(arg, name) for arg, name in zip(args, argnames)])

    tpl = f"""
$Function {rtype} {name}({args})
"""

    vcc_fh.write(tpl)


def c_arg(arg, n):
    match = re_arg.search(arg)
    if match is None:
        print(f"Error: no re_arg match on '{arg}'")
        sys.exit(1)
    # print(match)

    atype = "VCL_" + vcc_type_map[match.group(1)]
    name = match.group(2)

    if name is None:
        name = n

    return f"{atype} {name}"


def gen_func_c(match, c_fh):
    rtype = match.group(1)
    name = match.group(2)
    args = match.group(3)

    # print(match)

    rtype = "VCL_" + vcc_type_map[rtype]
    args = re_argsep.split(args)

    argnames = def_arg_names[: len(args)]
    if name in arg_names_map:
        argnames = arg_names_map[name]

    args = ", ".join([c_arg(arg, name) for arg, name in zip(args, argnames)])
    argnames = ", ".join(argnames)

    tpl = f"""
{rtype}
vmod_{name}(VRT_CTX, {args})
{{
	(void)ctx;
	return ({name}({argnames}));
}}
"""

    c_fh.write(tpl)


def gen_func(func, vcc_fh, c_fh):
    gen_func_vcc(func, vcc_fh)
    gen_func_c(func, c_fh)


# rtype name (args...)
re_spec_funcs = re.compile(r"\b([a-z0-9 ]+?)\s+([a-z0-9]+)\((.*)\)(?=;)")
# disregard variants for types we do not need or can not support
# not support: frexp(double, int *) etc returning two values
re_func_disregard = re.compile(r"long double|float\b|long long|int \*|double \*")


def gen_funcs(spec, vcc_fh, c_fh):
    header = """

math.h functions
----------------

The semantics of functions mapping directly to `math.h(7)` are not documented
herein, see the system documentation instead (for example using ``man
<function>``).

"""
    vcc_fh.write(header)

    for match in re_spec_funcs.finditer(spec):
        f = match.group(0)
        # print("XXX " + match.group(0))
        if re_func_disregard.search(f):
            # print(f"disregard: {f}")
            continue
        gen_func(match, vcc_fh, c_fh)


def gen_enumfunc(ret, func, enums, doc, vcc_fh, c_fh):
    enumcomma = ", ".join(enums)

    tpl = f"""
$Function {ret} {func}(ENUM {{ {enumcomma} }} name)

{doc}
"""

    vcc_fh.write(tpl)

    pre = f"""
VCL_{ret}
vmod_{func}(VRT_CTX, VCL_ENUM name)
{{
    (void)ctx;
"""

    post = f"""
    WRONG("{func} enum");
}}
"""

    c_fh.write(pre)
    for enum in enums:
        body = f"""
    if (name == VENUM({enum})) return ({enum});
"""
        c_fh.write(body)
    c_fh.write(post)


re_spec_const = re.compile(r">(M_[A-Z0-9_]+)<")
# Austin Group Defect 1503 - not yet available on Linux as of Debian 12.12 2025-12
re_const_disregard = re.compile("^(M_1_SQRTPI|M_EGAMMA|M_PHI|M_SQRT1_3|M_SQRT3)$")


def gen_const(spec, vcc_fh, c_fh):
    a = [
        # DBL_* from float.h
        "DBL_MANT_DIG",
        "DBL_DIG",
        "DBL_MIN_EXP",
        "DBL_MIN_10_EXP",
        "DBL_MAX_EXP",
        "DBL_MAX_10_EXP",
        "DBL_MAX",
        "DBL_EPSILON",
        "DBL_MIN",
        # HUGE_VAL not worth extracting from html
        "HUGE_VAL",
    ]

    for match in re_spec_const.finditer(spec):
        m = match.group(1)
        if re_const_disregard.search(m):
            continue
        a.append(m)

    doc = """
Return the value of the named constant. For ``DBL_*`` see `float.h(7)`,
otherwise `math.h(7)` for details.
"""
    gen_enumfunc("REAL", "constant", a, doc, vcc_fh, c_fh)


# not worth grepping the FP_ macros from the html
def gen_class(spec, vcc_fh, c_fh):
    # not worth extracting from html
    a = ["FP_INFINITE", "FP_NAN", "FP_NORMAL", "FP_SUBNORMAL", "FP_ZERO"]

    doc = """
Return the value of the named constant for comparisons of `math.fpclassify()`_
return values.

"""

    gen_enumfunc("INT", "fpclass", a, doc, vcc_fh, c_fh)


vcc_top = """
#-
# This document is licensed under the same conditions as Varnish-Cache itself.
# See LICENSE for details.
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Author: Nils Goroll <nils.goroll@uplex.de>

$ABI vrt
$Module math 3 "VMOD wrapping math.h"

.. THIS CODE IS AUTO-GENERATED. DO NOT EDIT HERE. see vmod_math_gen.py

DESCRIPTION
===========

This VMOD wraps the functions in `math.h(7)`, provides additional utilities and
includes functions to access macros and constants from `math.h(7)` and
`float.h(7)`.

Utility functions
-----------------

.. source is in vmod_math_util.c

$Function BOOL approx(REAL a, REAL b, REAL maxDiff = 0.0, REAL maxRelDiff = 0.0)

.. _`Comparing Floating Point Numbers, 2012 Edition`: https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/

Return true if the two numbers *a* and *b* are approximately equal per the
``AlmostEqualRelativeAndAbs`` function from `Comparing Floating Point Numbers,
2012 Edition`_ given the additional parameters *maxDiff* and *maxRelDiff*. Read
the blog post for details on why the seemingly simple question of two floating
point numbers being equal has no simple answers.

For their default values of 0, the two additional parameters are initialized to:

- *maxDiff*: ``4 * DBL_EPSILON``
- *maxRelDiff*: ``DBL_EPSILON``

$Function STRING strfromd(STRING format, REAL fp)

Convert the value *fp* into a string using *format*, see `strfromd(3)`.

An invalid format string results in a VCL error and the ``NULL`` invalid string
returned.

.. Internally, vsnprintf() is called via WS_Printf(). The difference to
   VRT_REAL_string() is the free format and missing call to VRT_REAL_is_valid()

Access to macros and constants
------------------------------

"""

c_top = """
/* this code is auto-generated. Do not edit */
#include "config.h"

#include <float.h>
#include <math.h>

#include "vdef.h"
#include "vas.h"
#include "vrt.h"
#include "vcc_math_if.h"

// macros in math.h code are generic-ish across float/double/long double
//lint --e{506} Constant value Boolean
//lint --e{736} Loss of precision
"""


def main():
    # Check command line arguments
    if len(sys.argv) != 4:
        print("Usage: python3 script.py <spec_file> <vcc_file> <c_file>")
        sys.exit(1)

    spec_file = sys.argv[1]
    vcc_file = sys.argv[2]
    c_file = sys.argv[3]

    spec = ""

    try:
        with open(spec_file, "r") as f:
            spec = f.read()
    except FileNotFoundError:
        print(f"Error: Input file '{spec_file}' not found")
        sys.exit(1)

    with open(vcc_file, "w") as vcc_fh:
        vcc_fh.write(vcc_top)
        with open(c_file, "w") as c_fh:
            c_fh.write(c_top)
            gen_const(spec, vcc_fh, c_fh)
            gen_class(spec, vcc_fh, c_fh)
            gen_funcs(spec, vcc_fh, c_fh)
    print(f"Output written to '{vcc_file}' and '{c_file}'")


if __name__ == "__main__":
    main()
