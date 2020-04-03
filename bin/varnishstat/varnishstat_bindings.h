/*-
 * Copyright (c) 2019 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
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
 */

/*lint -save -e525 -e539 */

#ifndef BINDING_KEY
#  define BINDING_KEY(key, name, next)
#endif

#define BINDING_CTRL(c) ((c) & 0x1f)

BINDING_KEY('h',	"h",)
BINDING(HELP, "\tToggle the help screen.")

BINDING_KEY(KEY_UP,	"UP",	" or ")
BINDING_KEY('k',	"k",)
BINDING(UP, "\tNavigate the counter list one line up.")

BINDING_KEY(KEY_DOWN,	"DOWN",	" or ")
BINDING_KEY('j',	"j",)
BINDING(DOWN, "\tNavigate the counter list one line down.")

BINDING_KEY(KEY_PPAGE,		"PAGEUP",	" or ")
BINDING_KEY('b',		"b",		" or ")
BINDING_KEY(BINDING_CTRL('b'),	"CTRL-B",)
BINDING(PAGEUP, "\tNavigate the counter list one page up.")

BINDING_KEY(KEY_NPAGE,		"PAGEDOWN",	" or ")
BINDING_KEY(' ',		"SPACE",	" or ")
BINDING_KEY(BINDING_CTRL('f'),	"CTRL-F",)
BINDING(PAGEDOWN, "\tNavigate the counter list one page down.")

BINDING_KEY(KEY_HOME,	"HOME",	" or ")
BINDING_KEY('g',	"g",)
BINDING(TOP, "\tNavigate the counter list to the top.")

BINDING_KEY(KEY_END,	"END",	" or ")
BINDING_KEY('G',	"G",)
BINDING(BOTTOM, "\tNavigate the counter list to the bottom.")

BINDING_KEY('d', "d",)
BINDING(UNSEEN,
    "\tToggle between showing and hiding unseen counters. Unseen\n"
    "\tcounters are those that has been zero for the entire runtime\n"
    "\tof varnishstat. Defaults to hide unseen counters."
)

BINDING_KEY('e', "e",)
BINDING(SCALE, "\tToggle scaling of values.")

BINDING_KEY('v', "v",)
BINDING(VERBOSE,
    "\tIncrease verbosity. Defaults to only showing informational\n"
    "\tcounters."
)

BINDING_KEY('V', "V",)
BINDING(QUIET,
    "\tDecrease verbosity. Defaults to only showing informational\n"
    "\tcounters."
)

BINDING_KEY('q', "q",)
BINDING(QUIT, "\tQuit.")

BINDING_KEY(BINDING_CTRL('t'), "CTRL+T",)
BINDING(SAMPLE, "\tSample now.")

BINDING_KEY('+', "+",)
BINDING(ACCEL, "\tIncrease refresh interval.")

BINDING_KEY('-', "-",)
BINDING(DECEL, "\tDecrease refresh interval.")

#ifdef BINDING_SIG
BINDING_KEY(BINDING_CTRL('c'), "CTRL+C",)
BINDING(SIG_INT, "")

BINDING_KEY(BINDING_CTRL('z'), "CTRL+Z",)
BINDING(SIG_TSTP, "")
#  undef BINDING_SIG
#endif

#undef BINDING_KEY
#undef BINDING_CTRL
#undef BINDING

/*lint -restore */

