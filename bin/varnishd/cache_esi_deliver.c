/*-
 * Copyright (c) 2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id")

#include <stdlib.h>
#include <stdio.h>

#include "cache.h"
#include "cache_esi.h"
#include "vend.h"
#include "vct.h"
#include "stevedore.h"


#ifndef OLD_ESI

void
ESI_Deliver(struct sess *sp)
{
	struct storage *st;
	uint8_t *p, *e, *q;
	unsigned off;
	size_t l;

printf("DELIV\n");
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	st = sp->obj->esidata;
	AN(st);
	p = st->ptr;
	e = st->ptr + st->len;

	st = VTAILQ_FIRST(&sp->obj->store);
	off = 0;

	while (p < e) {
		switch (*p) {
		case VEC_V1:
			l = p[1];
			p += 2;
			q = (void*)strchr((const char*)p, '\0');
			assert (q > p);
			printf("VER(%d) %d\n", (int)l, (int)(q-p));
			if (sp->wrk->res_mode & RES_CHUNKED)
				WRW_Write(sp->wrk, p, q - p);
			WRW_Write(sp->wrk, st->ptr + off, l);
			if (sp->wrk->res_mode & RES_CHUNKED)
				WRW_Write(sp->wrk, "\r\n", -1);
			// printf("[%.*s]", (int)l, st->ptr + off);
			off += l;
			p = q + 1;
			break;
		case VEC_S1:
			l = p[1];
			p += 2;
			printf("SKIP(%d)\n", (int)l);
			off += l;
			break;
		case VEC_L1:
			l = p[1];
			p += 2;
			q = (void*)strchr((const char*)p, '\0');
			assert (q > p);
			printf("LIT(%d) %d\n", (int)l, (int)(q-p));
			if (sp->wrk->res_mode & RES_CHUNKED)
				WRW_Write(sp->wrk, p, q - p);
			p = q + 1;
			WRW_Write(sp->wrk, p, l);
			if (sp->wrk->res_mode & RES_CHUNKED)
				WRW_Write(sp->wrk, "\r\n", -1);
			p = p + l;
			break;
		default:
			printf("XXXX %02x [%c]\n", *p, *p);
			INCOMPL();
			return;
		}
	}
	WRW_Flush(sp->wrk);
}

#endif /* OLD_ESI */
