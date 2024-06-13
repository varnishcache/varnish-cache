/*-
 * Copyright (c) 2016 Varnish Software AS
 * All rights reserved.
 *
 * Martin Blix Grydeland <martin@varnish-software.com>
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

#include "config.h"

#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "vdef.h"
#include "vas.h"

static unsigned minlen = UINT_MAX;
static unsigned maxlen = 0;
static unsigned idx = 0;

static const struct {
	uint32_t	code;
	unsigned	blen;
	char		chr;
} huf[] = {
#define HPH(c, h, l) { h, l, (char)c },
#include "tbl/vhp_huffman.h"
};

#define HUF_LEN (sizeof huf / sizeof huf[0])

struct tbl;

struct cod {
	uint32_t		bits;
	unsigned		len;
	uint8_t			chr;
	struct tbl		*next;
};

struct tbl {
	unsigned		mask;
	uint32_t		code;
	unsigned		masked;
	unsigned		n;
	unsigned		idx;
	unsigned		lvl;
	unsigned		p_idx;
	struct cod		e[] v_counted_by_(n);
};

static struct tbl *
tbl_new(unsigned mask)
{
	unsigned n;
	size_t size;
	struct tbl *tbl;

	assert(mask > 0);
	assert(mask <= 8);
	n = 1U << mask;
	size = sizeof (struct tbl) + n * sizeof (struct cod);
	tbl = calloc(1, size);
	AN(tbl);
	memset(tbl, 0, size);
	tbl->mask = mask;
	tbl->n = n;
	tbl->idx = idx;
	idx += n;
	return (tbl);
}

static void
tbl_free(struct tbl* table)
{
	for (unsigned i = 0; i < table->n; i++) {
		if (table->e[i].next != NULL)
			tbl_free(table->e[i].next);
	}
	free(table);
}

static void
tbl_add(struct tbl *tbl, uint32_t code, unsigned codelen,
    uint32_t bits, unsigned len, char chr)
{
	uint32_t b;
	unsigned u;

	AN(tbl);
	assert(codelen > 0);
	assert(codelen <= maxlen);
	assert(len > 0);
	assert(tbl->mask > 0);

	if (len > tbl->mask) {
		/* Does not belong in this table */
		b = bits >> (len - tbl->mask);
		bits &= (1U << (len - tbl->mask)) - 1;
		if (tbl->e[b].next == NULL) {
			tbl->e[b].len = tbl->mask;
			tbl->e[b].next = tbl_new(len - tbl->mask);
			AN(tbl->e[b].next);

			tbl->e[b].next->masked = tbl->masked + tbl->mask;
			tbl->e[b].next->code = code;
			tbl->e[b].next->lvl = tbl->lvl + 1;
			tbl->e[b].next->p_idx = tbl->idx + b;
		}
		AN(tbl->e[b].next);
		tbl_add(tbl->e[b].next, code, codelen,
		    bits, len - tbl->mask, chr);
		return;
	}

	bits = bits << (tbl->mask - len);
	for (u = 0; u < (1U << (tbl->mask - len)); u++) {
		b = bits | u;
		assert(b < tbl->n);
		AZ(tbl->e[b].len);
		AZ(tbl->e[b].next);
		tbl->e[b].len = len;
		tbl->e[b].chr = chr;
	}
}

static void
print_lsb(uint32_t c, int l)
{
	assert(l <= 32);

	while (l > 0) {
		if (c & (1U << (l - 1)))
			printf("1");
		else
			printf("0");
		l--;
	}
}

static void
tbl_print(const struct tbl *tbl)
{
	unsigned u;

	printf("/* Table: lvl=%u p_idx=%u n=%u mask=%u masked=%u */\n",
	    tbl->lvl, tbl->p_idx, tbl->n, tbl->mask, tbl->masked);
	for (u = 0; u < tbl->n; u++) {
		printf("/* %3u: ", tbl->idx + u);
		printf("%*s", maxlen - tbl->mask - tbl->masked, "");
		printf("%*s", tbl->mask - tbl->e[u].len, "");

		if (tbl->masked > 0) {
			printf("(");
			print_lsb(tbl->code >> tbl->mask, tbl->masked);
			printf(") ");
		} else
			printf("   ");
		if (tbl->e[u].len < tbl->mask) {
			print_lsb(u >> (tbl->mask - tbl->e[u].len),
			    tbl->e[u].len);
			printf(" (");
			print_lsb(u, tbl->mask - tbl->e[u].len);
			printf(")");
		} else {
			assert(tbl->e[u].len == tbl->mask);
			print_lsb(u, tbl->e[u].len);
			printf("   ");
		}
		printf("%*s", 3 - (tbl->mask - tbl->e[u].len), "");
		printf(" */ ");

		if (tbl->e[u].next) {
			/* Jump to next table */
			assert(tbl->e[u].next->idx - (tbl->idx + u)
			    <= UINT8_MAX);
			printf("{ .len = %u, .jump = %u },",
			    tbl->e[u].len,
			    tbl->e[u].next->idx - (tbl->idx + u));
			printf(" /* Next: %u */", tbl->e[u].next->idx);
		} else if (tbl->e[u].len) {
			printf("{ ");
			printf(".len = %u", tbl->e[u].len);
			printf(", .chr = (char)0x%02x", tbl->e[u].chr);
			if (isgraph(tbl->e[u].chr))
				printf(" /* '%c' */", tbl->e[u].chr);
			if (u == 0)
				/* First in table, set mask */
				printf(", .mask = %u", tbl->mask);
			printf(" },");
		} else
			printf("{ .len = 0 }, /* invalid */");
		printf("\n");
	}

	for (u = 0; u < tbl->n; u++)
		if (tbl->e[u].next)
			tbl_print(tbl->e[u].next);
}

int
main(int argc, const char **argv)
{
	struct tbl *top;
	unsigned u;

	(void)argc;
	(void)argv;

	for (u = 0; u < HUF_LEN; u++) {
		maxlen = vmax(maxlen, huf[u].blen);
		minlen = vmin(minlen, huf[u].blen);
	}

	top = tbl_new(8);
	AN(top);

	for (u = 0; u < HUF_LEN; u++)
		tbl_add(top, huf[u].code, huf[u].blen,
		    huf[u].code, huf[u].blen, huf[u].chr);

	printf("/*\n");
	printf(" * NB:  This file is machine generated, DO NOT EDIT!\n");
	printf(" *\n");
	printf(" */\n\n");

	printf("#define HUFDEC_LEN %u\n", idx);
	printf("#define HUFDEC_MIN %u\n", minlen);
	printf("#define HUFDEC_MAX %u\n\n", maxlen);

	printf("static const struct {\n");
	printf("\tuint8_t\tmask;\n");
	printf("\tuint8_t\tlen;\n");
	printf("\tuint8_t\tjump;\n");
	printf("\tchar\tchr;\n");
	printf("} hufdec[HUFDEC_LEN] = {\n");
	tbl_print(top);
	printf("};\n");

	tbl_free(top);
	return (0);
}
