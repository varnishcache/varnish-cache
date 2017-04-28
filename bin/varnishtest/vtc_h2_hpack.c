/*-
 * Copyright (c) 2008-2016 Varnish Software AS
 * All rights reserved.
 *
 * Author: Guillaume Quintard <guillaume.quintard@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vas.h>

#include "hpack.h"
#include "vtc_h2_priv.h"

struct symbol {
	uint32_t	val;
	uint8_t		size;
};

static const struct symbol coding_table[] = {
#define HPACK(i, v, l) {v, l},
#include "vtc_h2_enctbl.h"
#undef HPACK
	{0, 0}
};

#include "vtc_h2_dectbl.h"

#define MASK(pack, n) (pack >> (64 - n))
static int
huff_decode(char *str, int nm, struct hpk_iter *iter, int ilen)
{
	int l = 0;
	uint64_t pack = 0;
	unsigned pl = 0; /* pack length*/
	struct stbl *tbl = &byte0;
	struct ssym *sym;

	(void)nm;
	while (ilen > 0 || pl != 0) {
		/* make sure we have enough data*/
		if (pl < tbl->msk) {
			if (ilen == 0) {
				if (pl == 0 || (MASK(pack, pl) ==
						(unsigned)((1 << pl) - 1))) {
					assert(tbl == &byte0);
					return (l);
				}
			}
			/* fit as many bytes as we can in pack */
			while (pl <= 56 && ilen > 0) {
				pack |= (uint64_t)(*iter->buf & 0xff)
					<< (56 - pl);
				pl += 8;
				iter->buf++;
				ilen--;
			}
		}
		assert(tbl);
		assert(tbl->msk);
		sym = &tbl->syms[MASK(pack, tbl->msk)];

		assert(sym->csm <= tbl->msk);

		if (sym->csm == 0 || pl < sym->csm)
			return (0);

		assert(sym->csm <= 8);
		pack <<= sym->csm;
		assert(sym->csm <= pl);
		pl -= sym->csm;
		if (sym->nxt) {
			tbl = sym->nxt;
			continue;
		}
		str[l++] = sym->chr;
		tbl = &byte0;
	}
	return (l);
}

/* inspired from Dridi Boukelmoune's cashpack. */
static enum hpk_result
huff_encode(struct hpk_iter *iter, const char *str, int len)
{
	uint64_t pack = 0;
	int pl = 0; /* pack length*/
	uint32_t	v;
	uint8_t		s;

	assert(iter->buf < iter->end);

	while (len--) {
		v = coding_table[(uint8_t)*str].val;
		s = coding_table[(uint8_t)*str].size;

		pl += s;
		pack |= (uint64_t)v << (64 - pl);

		while (pl >= 8) {
			if (iter->buf == iter->end)
				return (hpk_done);
			*iter->buf = (char)(pack >> 56);
			iter->buf++;
			pack <<= 8;
			pl -= 8;
		}
		str++;
	}

	/* padding */
	if (pl) {
		assert(pl < 8);
		if (iter->buf == iter->end)
			return (1);
		pl += 8;
		pack |= (uint64_t)0xff << (64 - pl);
		*iter->buf = (char)(pack >> 56);
		iter->buf++;
	}

	return (hpk_more);
}

static int
huff_simulate(const char *str, int ilen, int huff)
{
	int olen = 0;
	if (!huff || !ilen)
		return (ilen);
	while (ilen--) {
		olen += coding_table[(unsigned char)*str].size;
		str++;
	}
	return ((olen+7)/8);
}

static enum hpk_result
num_decode(uint32_t *result, struct hpk_iter *iter, uint8_t prefix)
{
	uint8_t shift = 0;

	assert(iter->buf < iter->end);
	assert(prefix);
	assert(prefix <= 8);

	*result = 0;
	*result = *iter->buf & (0xff >> (8-prefix));
	if (*result < (1 << prefix) - 1) {
		iter->buf++;
		return (ITER_DONE(iter));
	}
	do {
		iter->buf++;
		if (iter->end == iter->buf)
			return (hpk_err);
		/* check for overflow */
		if ((UINT32_MAX - *result) >> shift < (*iter->buf & 0x7f))
			return (hpk_err);

		*result += (uint32_t)(*iter->buf & 0x7f) << shift;
		shift += 7;
	} while (*iter->buf & 0x80);
	iter->buf++;

	return (ITER_DONE(iter));
}

static enum hpk_result
num_encode(struct hpk_iter *iter, uint8_t prefix, uint32_t num)
{
	assert(prefix);
	assert(prefix <= 8);
	assert(iter->buf < iter->end);

	uint8_t pmax = (1 << prefix) - 1;

	*iter->buf &= 0xff << prefix;
	if (num <=  pmax) {
		*iter->buf++ |= num;
		return (ITER_DONE(iter));
	} else if (iter->end - iter->buf < 2)
		return (hpk_err);

	iter->buf[0] |= pmax;
	num -= pmax;
	do {
		iter->buf++;
		if (iter->end == iter->buf)
			return (hpk_err);
		*iter->buf = num % 128;
		*iter->buf |= 0x80;
		num /= 128;
	} while (num);
	*iter->buf++ &= 127;
	return (ITER_DONE(iter));
}

static enum hpk_result
str_encode(struct hpk_iter *iter, const struct txt *t)
{
	int slen = huff_simulate(t->ptr, t->len, t->huff);
	assert(iter->buf < iter->end);
	if (t->huff)
		*iter->buf = 0x80;
	else
		*iter->buf = 0;

	if (hpk_err == num_encode(iter, 7, slen))
		return (hpk_err);

	if (slen > iter->end - iter->buf)
		return (hpk_err);

	if (t->huff) {
		return (huff_encode(iter, t->ptr, t->len));
	} else {
		memcpy(iter->buf, t->ptr, slen);
		iter->buf += slen;
		return (ITER_DONE(iter));
	}
}

static enum hpk_result
str_decode(struct hpk_iter *iter, struct txt *t)
{
	uint32_t num;
	int huff;
	assert(iter->buf < iter->end);
	huff = (*iter->buf & 0x80);
	if (hpk_more != num_decode(&num, iter, 7))
		return (hpk_err);
	if (num > iter->end - iter->buf)
		return (hpk_err);
	if (huff) { /*Huffman encoding */
		t->ptr = malloc((num * 8) / 5L + 1L);
		AN(t->ptr);
		num = huff_decode(t->ptr, (num * 8) / 5, iter, num);
		if (!num) {
			free(t->ptr);
			return (hpk_err);
		}
		t->huff = 1;
		/* XXX: do we care? */
		t->ptr = realloc(t->ptr, num + 1L);
		AN(t->ptr);
	} else { /* literal string */
		t->huff = 0;
		t->ptr = malloc(num + 1L);
		AN(t->ptr);
		memcpy(t->ptr, iter->buf, num);
		iter->buf += num;
	}

	t->ptr[num] = '\0';
	t->len = num;

	return (ITER_DONE(iter));
}

static inline void
txtcpy(struct txt *to, const struct txt *from)
{
	//AZ(to->ptr);
	to->ptr = malloc(from->len + 1L);
	AN(to->ptr);
	memcpy(to->ptr, from->ptr, from->len + 1L);
	to->len = from->len;
}

int
gethpk_iterLen(const struct hpk_iter *iter)
{
	return (iter->buf - iter->orig);
}

enum hpk_result
HPK_DecHdr(struct hpk_iter *iter, struct hpk_hdr *header)
{
	int pref = 0;
	const struct txt *t;
	uint32_t num;
	int must_index = 0;
	assert(iter);
	assert(iter->buf < iter->end);
	/* Indexed Header Field */
	if (*iter->buf & 128) {
		header->t = hpk_idx;
		if (hpk_err == num_decode(&num, iter, 7))
			return (hpk_err);

		if (num) { /* indexed key and value*/
			t = tbl_get_key(iter->ctx, num);
			if (!t)
				return (hpk_err);
			txtcpy(&header->key, t);

			t = tbl_get_value(iter->ctx, num);
			if (!t) {
				free(header->key.ptr);
				return (hpk_err);
			}

			txtcpy(&header->value, t);

			if (iter->buf < iter->end)
				return (hpk_more);
			else
				return (hpk_done);
		} else
			return (hpk_err);

	}
	/* Literal Header Field with Incremental Indexing */
	else if (*iter->buf >> 6 == 1) {
		header->t = hpk_inc;
		pref = 6;
		must_index = 1;
	}
	/* Literal Header Field without Indexing */
	else if (*iter->buf >> 4 == 0) {
		header->t = hpk_not;
		pref = 4;
	}
	/* Literal Header Field never Indexed */
	else if (*iter->buf >> 4 == 1) {
		header->t = hpk_never;
		pref = 4;
	}
	/* Dynamic Table Size Update */
	/* XXX if under max allowed value */
	else if (*iter->buf >> 5 == 1) {
		if (hpk_done != num_decode(&num, iter, 5))
			return (hpk_err);
		return HPK_ResizeTbl(iter->ctx, num);
	} else {
		return (hpk_err);
	}

	assert(pref);
	if (hpk_more != num_decode(&num, iter, pref))
		return (hpk_err);

	header->i = num;
	if (num) { /* indexed key */
		t = tbl_get_key(iter->ctx, num);
		if (!t)
			return (hpk_err);
		txtcpy(&header->key, t);
	} else {
		if (hpk_more != str_decode(iter, &header->key))
			return (hpk_err);
	}

	if (hpk_err == str_decode(iter, &header->value))
		return (hpk_err);

	if (must_index)
		push_header(iter->ctx, header);
	return (ITER_DONE(iter));
}

enum hpk_result
HPK_EncHdr(struct hpk_iter *iter, const struct hpk_hdr *h)
{
	int pref;
	int must_index = 0;
	enum hpk_result ret;
	switch (h->t) {
		case hpk_idx:
			*iter->buf = 0x80;
			num_encode(iter, 7, h->i);
			return (ITER_DONE(iter));
		case hpk_inc:
			*iter->buf = 0x40;
			pref = 6;
			must_index = 1;
			break;
		case hpk_not:
			*iter->buf = 0x00;
			pref = 4;
			break;
		case hpk_never:
			*iter->buf = 0x10;
			pref = 4;
			break;
		default:
			INCOMPL();
	}
	if (h->i) {
		if (hpk_more != num_encode(iter, pref, h->i))
			return (hpk_err);
	} else {
		iter->buf++;
		if (hpk_more != str_encode(iter, &h->key))
			return (hpk_err);
	}
	ret = str_encode(iter, &h->value);
	if (ret == hpk_err)
		return (hpk_err);
	if (must_index)
		push_header(iter->ctx, h);
	return (ret);

}
