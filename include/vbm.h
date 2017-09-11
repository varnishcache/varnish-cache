/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Varnish Software AS
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
 * Self-sizeing bitmap operations
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/**********************************************************************
 * Generic bitmap functions
 */

#define VBITMAP_TYPE	unsigned	/* Our preferred wordsize */
#define VBITMAP_LUMP	(1024)		/* How many bits we alloc at a time */
#define VBITMAP_WORD	(sizeof(VBITMAP_TYPE) * 8)
#define VBITMAP_IDX(n)	((n) / VBITMAP_WORD)
#define VBITMAP_BIT(n)	(1U << ((n) % VBITMAP_WORD))

static inline unsigned
vbit_rndup(unsigned bit, unsigned to)
{
	bit += to - 1;
	bit -= (bit % to);

	return (bit);
}

struct vbitmap {
	unsigned	flags;
#define VBITMAP_FL_MALLOC	 1	/* struct vbitmap is malloced */
#define VBITMAP_FL_MALLOC_BITS	(1<<1)	/* bits space is malloced */

	VBITMAP_TYPE	*bits;
	unsigned	nbits;
};

static inline void
vbit_expand(struct vbitmap *vb, unsigned bit)
{
	unsigned char *p;

	bit = vbit_rndup(bit, VBITMAP_LUMP);
	assert(bit > vb->nbits);

	if (vb->flags & VBITMAP_FL_MALLOC_BITS) {
		p = realloc(vb->bits, bit / 8);
		assert(p != NULL);
	} else {
		p = malloc(bit / 8);
		assert(p != NULL);
		if (vb->nbits > 0)
			memcpy(p, vb->bits, vb->nbits / 8);
	}
	memset(p + vb->nbits / 8, 0, (bit - vb->nbits) / 8);
	vb->flags |= VBITMAP_FL_MALLOC_BITS;
	vb->bits = (void*)p;
	vb->nbits = bit;
}

#define VBITMAP_SZ(b) (sizeof(struct vbitmap) + \
	vbit_rndup(b, VBITMAP_WORD))

/*
 * init from some extent of memory (e.g. workspace) which the caller must
 * manage. Returns a vbitmap with as many bits as fit into sz in VBITMAP_WORD
 * chunks.
 *
 * use VBITMAP_SZ to calculate sz
 */
static inline struct vbitmap *
vbit_init(void *p, size_t sz)
{
	struct vbitmap *vb;

	if (sz < sizeof(*vb))
		return NULL;

	memset(p, 0, sz);
	vb = p;

	p = (char *)p + sizeof(*vb);
	sz -= sizeof(*vb);

	vb->nbits = (sz / VBITMAP_WORD) * VBITMAP_WORD;
	if (vb->nbits)
		vb->bits = p;

	return (vb);
}

/* init using malloc */
static inline struct vbitmap *
vbit_new(unsigned initial)
{
	struct vbitmap *vb;

	vb = calloc(1, sizeof *vb);
	assert(vb != NULL);
	vb->flags |= VBITMAP_FL_MALLOC;
	if (initial == 0)
		initial = VBITMAP_LUMP;
	vbit_expand(vb, initial);
	return (vb);
}

static inline void
vbit_destroy(struct vbitmap *vb)
{

	if (vb == NULL)
		return;
	if (vb->flags & VBITMAP_FL_MALLOC_BITS) {
		free(vb->bits);
		vb->bits = NULL;
		vb->nbits = 0;
	}
	if (vb->flags & VBITMAP_FL_MALLOC)
		free(vb);
}

static inline void
vbit_set(struct vbitmap *vb, unsigned bit)
{

	if (bit >= vb->nbits)
		vbit_expand(vb, bit + 1);
	vb->bits[VBITMAP_IDX(bit)] |= VBITMAP_BIT(bit);
}

static inline void
vbit_clr(const struct vbitmap *vb, unsigned bit)
{

	if (bit < vb->nbits)
		vb->bits[VBITMAP_IDX(bit)] &= ~VBITMAP_BIT(bit);
}

static inline int
vbit_test(const struct vbitmap *vb, unsigned bit)
{

	if (bit >= vb->nbits)
		return (0);
	return (vb->bits[VBITMAP_IDX(bit)] & VBITMAP_BIT(bit));
}
