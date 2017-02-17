/*-
 * Copyright (c) 2008-2017 Varnish Software AS
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
 */

#include "config.h"

#include <sys/types.h>

#include "vtc.h"

#include "vct.h"

struct vsb *
vtc_hex_to_bin(struct vtclog *vl, const char *arg)
{
	struct vsb *vsb;
	unsigned sh = 4;
	unsigned c, b = 0;

	vsb = VSB_new_auto();
	AN(vsb);
	for (; *arg != '\0'; arg++) {
		if (vct_issp(*arg))
			continue;
		c = (uint8_t)*arg;
		if (c >= '0' && c <= '9')
			b |= (c - 48U) << sh;
		else if (c >= 'A' && c <= 'F')
			b |= (c - 55U) << sh;
		else if (c >= 'a' && c <= 'f')
			b |= (c - 87U) << sh;
		else
			vtc_fatal(vl,"Illegal hex string");
		sh = 4 - sh;
		if (sh == 4) {
			VSB_putc(vsb, b);
			b = 0;
		}
	}
	if (sh != 4)
		VSB_putc(vsb, b);
	AZ(VSB_finish(vsb));
	return (vsb);
}
