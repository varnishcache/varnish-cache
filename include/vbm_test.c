/*-
 * Copyright 2016 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Nils Goroll <nils.goroll@uplex.de>
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
 *
 * Test Self-sizing bitmap operations static initialization with dynamic growth
 */

#include <assert.h>
#include <stdio.h>

#include "vbm.h"

int
main(void)
{

	const unsigned sz = VBITMAP_SZ(1);
	char spc[sz];
	struct vbitmap *vb = vbit_init(spc, sz);

	VBITMAP_TYPE	*obits;
	unsigned	nbits;

	assert(vb);
	obits = vb->bits;
	nbits = vb->nbits;
	assert(nbits == VBITMAP_WORD);

	vbit_set(vb, nbits - 1);
	assert(vbit_test(vb, nbits - 1));

	assert(vb->bits);
	/* nothing malloc'ed - null ops */
	vbit_destroy(vb);
	assert(vb->bits);
	assert(vb->bits == obits);

	/* re-alloc */
	vbit_set(vb, nbits);
	assert(vbit_test(vb, nbits - 1));
	assert(vbit_test(vb, nbits));
	assert(vb->nbits == VBITMAP_LUMP);
	assert(vb->bits != obits);
	assert(vb->flags & VBITMAP_FL_MALLOC_BITS);

	assert(vb->bits);
	/* free the bits */
	vbit_destroy(vb);
	assert(vb->bits == NULL);
	assert(vb->nbits == 0);

	/* use again */
	assert(20 < VBITMAP_LUMP);
	vbit_set(vb, 20);
	assert(vbit_test(vb, 20));
	assert(vb->nbits == VBITMAP_LUMP);
	assert(vb->flags & VBITMAP_FL_MALLOC_BITS);

	/* grow */
	vbit_set(vb, VBITMAP_LUMP);
	assert(vbit_test(vb, 20));
	assert(vbit_test(vb, VBITMAP_LUMP));
	assert(vb->nbits == 2 * VBITMAP_LUMP);
	assert(vb->flags & VBITMAP_FL_MALLOC_BITS);

	vbit_destroy(vb);

	return (0);
}
