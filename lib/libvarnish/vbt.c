/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2022 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Guillaume Quintard <guillaume@varnish-software.com>
 * Author: Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
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
 * We tend to print back-traces when there is a fatal error, so the VBT code
 * should avoid assertions.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef WITH_UNWIND
#  include <libunwind.h>
#endif

#ifdef HAVE_EXECINFO_H
#  include <execinfo.h>
#endif

#include "vdef.h"
#include "vas.h"
#include "vbt.h"
#include "vsb.h"

#include "miniobj.h"

#ifdef WITH_UNWIND
static int
vbt_unwind(struct vsb *vsb)
{
	unw_cursor_t cursor; unw_context_t uc;
	unw_word_t ip, sp;
	unw_word_t offp;
	char fname[1024];
	const char *sep;
	int ret;

	ret = unw_getcontext(&uc);
	if (ret != 0) {
		VSB_printf(vsb, "Backtrace not available "
		    "(unw_getcontext returned %d)\n", ret);
		return (-1);
	}
	ret = unw_init_local(&cursor, &uc);
	if (ret != 0) {
		VSB_printf(vsb, "Backtrace not available "
		    "(unw_init_local returned %d)\n", ret);
		return (-1);
	}
	while (unw_step(&cursor) > 0) {
		fname[0] = '\0';
		sep = "";
		if (!unw_get_reg(&cursor, UNW_REG_IP, &ip)) {
			VSB_printf(vsb, "ip=0x%lx", (long) ip);
			sep = " ";
		}
		if (!unw_get_reg(&cursor, UNW_REG_SP, &sp)) {
			VSB_printf(vsb, "%ssp=0x%lx", sep, (long) sp);
			sep = " ";
		}
		if (!unw_get_proc_name(&cursor, fname, sizeof(fname), &offp)) {
			VSB_printf(vsb, "%s<%s+0x%lx>",
			    sep, fname[0] ? fname : "<unknown>", (long)offp);
		}
		VSB_putc(vsb, '\n');
	}

	return (0);
}
#endif

#ifdef HAVE_EXECINFO_H
#  define BACKTRACE_LEVELS	20

static void
vbt_execinfo(struct vsb *vsb)
{
	void *array[BACKTRACE_LEVELS];
	size_t size;
	size_t i;
	char **strings;
	char *p;
	char buf[32];

	size = backtrace (array, BACKTRACE_LEVELS);
	if (size > BACKTRACE_LEVELS) {
		VSB_printf(vsb, "Backtrace not available (ret=%zu)\n", size);
		return;
	}
	for (i = 0; i < size; i++) {
		bprintf(buf, "%p", array[i]);
		VSB_printf(vsb, "%s: ", buf);
		strings = backtrace_symbols(&array[i], 1);
		if (strings == NULL || strings[0] == NULL) {
			VSB_cat(vsb, "(?)");
		} else {
			p = strings[0];
			if (!memcmp(buf, p, strlen(buf))) {
				p += strlen(buf);
				if (*p == ':')
					p++;
				while (*p == ' ')
					p++;
			}
			VSB_cat(vsb, p);
		}
		VSB_cat(vsb, "\n");
		free(strings);
	}
}
#endif

void
VBT_format(struct vsb *vsb)
{

	if (!VALID_OBJ(vsb, VSB_MAGIC))
		return;
#ifdef WITH_UNWIND
	if (!vbt_unwind(vsb))
		return;
#  ifdef HAVE_EXECINFO_H
	VSB_cat(vsb, "Falling back to execinfo backtrace\n");
#  endif
#endif

#ifdef HAVE_EXECINFO_H
	vbt_execinfo(vsb);
#endif
}

int
VBT_dump(size_t len, char buf[len])
{
	struct vsb vsb[1];

	if (buf == NULL || VSB_init(vsb, buf, len) == NULL)
		return (-1);

	VSB_printf(vsb, "Backtrace:\n");
	VSB_indent(vsb, 2);
	VBT_format(vsb);
	VSB_indent(vsb, -2);

	return (0);
}
