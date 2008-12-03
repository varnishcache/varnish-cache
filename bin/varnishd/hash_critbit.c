/*-
 * Copyright (c) 2008 Linpro AS
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
 * $Id$
 *
 * A Crit Bit tree based hash
 */

#include "config.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "shmlog.h"
#include "cache.h"
#include "hash_slinger.h"

static struct lock hcb_mtx;

/**********************************************************************
 * Table for finding out how many bits two bytes have in common,
 * counting from the MSB towards the LSB.
 * ie: 
 *	hcb_bittbl[0x01 ^ 0x22] == 2
 *	hcb_bittbl[0x10 ^ 0x0b] == 3
 * 
 */

static unsigned char hcb_bittbl[256];

static unsigned char
hcb_bits(unsigned char x, unsigned char y)
{
	return hcb_bittbl[x ^ y];
}

static void
hcb_build_bittbl(void)
{
	unsigned x, y;

	y = 0;
	for (x = 0; x < 8; x++)
		for (; y < (1U << x); y++)
			hcb_bittbl[y] = 8 - x;

	/* Quick asserts for sanity check */
	assert(hcb_bits(0x34, 0x34) == 8);
	assert(hcb_bits(0xaa, 0x55) == 0);
	assert(hcb_bits(0x01, 0x22) == 2);
	assert(hcb_bits(0x10, 0x0b) == 3);
}

/**********************************************************************
 * For space reasons we overload the two pointers with two different
 * kinds of of pointers.  We cast them to uintptr_t's and abuse the
 * low two bits to tell them apart, assuming that Varnish will never
 * run on machines with less than 32bit alignment.
 *
 * Asserts will explode if these assumptions are not met.
 */

struct hcb_y {
	unsigned short	critbit;
	unsigned char	ptr;
	unsigned char	bitmask;
	uintptr_t	leaf[2];
};

#define HCB_BIT_NODE		(1<<0)
#define HCB_BIT_Y		(1<<1)

struct hcb_root {
	uintptr_t	origo;
	unsigned	cmps;
};

static struct hcb_root	hcb_root;

/**********************************************************************
 * Pointer accessor functions
 */
static int
hcb_is_node(uintptr_t u)
{

	return (u & HCB_BIT_NODE);
}

static int
hcb_is_y(uintptr_t u)
{

	return (u & HCB_BIT_Y);
}

static uintptr_t
hcb_r_node(struct objhead *n)
{

	assert(!((uintptr_t)n & (HCB_BIT_NODE|HCB_BIT_Y)));
	return (HCB_BIT_NODE | (uintptr_t)n);
}

static struct objhead *
hcb_l_node(uintptr_t u)
{

	assert(u & HCB_BIT_NODE);
	assert(!(u & HCB_BIT_Y));
	return ((struct objhead *)(u & ~HCB_BIT_NODE));
}

static uintptr_t
hcb_r_y(struct hcb_y *y)
{

	assert(!((uintptr_t)y & (HCB_BIT_NODE|HCB_BIT_Y)));
	return (HCB_BIT_Y | (uintptr_t)y);
}

static struct hcb_y *
hcb_l_y(uintptr_t u)
{

	assert(!(u & HCB_BIT_NODE));
	assert(u & HCB_BIT_Y);
	return ((struct hcb_y *)(u & ~HCB_BIT_Y));
}

/**********************************************************************/

static unsigned
hcb_crit_bit(const struct objhead *oh1, const struct objhead *oh2, struct hcb_y *y)
{
	unsigned u, r;

	for (u = 0; u < oh1->digest_len && oh1->digest[u] == oh2->digest[u]; u++)
		;
	r = hcb_bits(oh1->digest[u], oh2->digest[u]);
	y->ptr = u;
	y->bitmask = 0x80 >> r;
	y->critbit = u * 8 + r;
	return (y->critbit);
}

/**********************************************************************/

static struct objhead *
hcb_insert(struct hcb_root *root, struct objhead *oh, int has_lock)
{
	uintptr_t *p;
	struct hcb_y *y, *y2;
	struct objhead *oh2;
	unsigned s, s2;


	p = &root->origo;
	if (*p == 0) {
		if (!has_lock)
			return (NULL);
		*p = hcb_r_node(oh);
		return (oh);
	}

	while(hcb_is_y(*p)) {
		y = hcb_l_y(*p);
		if (y->ptr > oh->digest_len)
			s = 0;
		else
			s = (oh->digest[y->ptr] & y->bitmask) != 0;
		assert(s < 2);
		root->cmps++;
		p = &y->leaf[s];
	}

	assert(hcb_is_node(*p));

	/* We found a node, does it match ? */
	oh2 = hcb_l_node(*p);
	if (oh2->digest_len == oh->digest_len &&
	    !memcmp(oh2->digest, oh->digest, oh->digest_len))
		return (oh2);

	if (!has_lock)
		return (NULL);

	/* Insert */

	y2 = (void*)&oh->u;
	memset(y2, 0, sizeof *y2);
	(void)hcb_crit_bit(oh, hcb_l_node(*p), y2);
	s2 = (oh->digest[y2->ptr] & y2->bitmask) != 0;
	assert(s2 < 2);
	y2->leaf[s2] = hcb_r_node(oh);
	s2 = 1-s2;

	p = &root->origo;
	assert(*p != 0);

	while(hcb_is_y(*p)) {
		y = hcb_l_y(*p);
		if (y->critbit > y2->critbit)
			break;
		if (y->ptr > oh->digest_len)
			s = 0;
		else
			s = (oh->digest[y->ptr] & y->bitmask) != 0;
		assert(s < 2);
		root->cmps++;
		p = &y->leaf[s];
	}
	y2->leaf[s2] = *p;
	*p = hcb_r_y(y2);
	return(oh);
}

/**********************************************************************/

static void
hcb_delete(struct hcb_root *r, struct objhead *oh)
{
	struct hcb_y *y;
	uintptr_t *p;
	unsigned s;

	if (r->origo == hcb_r_node(oh)) {
		r->origo = 0;
		return;
	}
	p = &r->origo;
	assert(hcb_is_y(*p));
	
	y = NULL;
	while(hcb_is_y(*p)) {
		y = hcb_l_y(*p);
		if (y->ptr > oh->digest_len)
			s = 0;
		else
			s = (oh->digest[y->ptr] & y->bitmask) != 0;
		assert(s < 2);
		if (y->leaf[s] == hcb_r_node(oh)) {
			*p = y->leaf[1 - s];
			y->leaf[0] = 0;
			y->leaf[1] = 0;
			return;
		}
		r->cmps++;
		p = &y->leaf[s];
	}
abort();
	*p = y->leaf[1 - s];
}

/**********************************************************************/

static void
dumptree(uintptr_t p, int indent, FILE *fd)
{
	int i;
	const struct objhead *oh;
	const struct hcb_y *y;

	if (p == 0)
		return;
	if (hcb_is_node(p)) {
		oh = hcb_l_node(p);
		fprintf(fd, "%*.*sN %u %u r%d <%02x%02x%02x...> <%s>\n",
		    indent, indent, "", oh->digest_len, indent / 2,
		    oh->refcnt,
		    oh->digest[0], oh->digest[1], oh->digest[2],
		    oh->hash);
		return;
	}
	assert(hcb_is_y(p));
	y = hcb_l_y(p);
	fprintf(fd, "%*.*sY c %u p %u b %02x i %d\n",
	    indent, indent, "",
	    y->critbit, y->ptr, y->bitmask, indent / 2);
	indent += 2;
	for (i = 0; i < 2; i++)
		dumptree(y->leaf[i], indent, fd);
}

static void
dump(const struct hcb_root *root, FILE *fd)
{
	fprintf(fd, "-------------\n");
	dumptree(root->origo, 0, fd);
	fprintf(fd, "-------------\n");
	fflush(fd);
}


/**********************************************************************/

static void
hcb_start(void)
{
	struct objhead *oh = NULL;

	(void)oh;
	assert(params->hash_sha256);
	assert(sizeof(struct hcb_y) <= sizeof(oh->u));
	memset(&hcb_root, 0, sizeof hcb_root);
	hcb_build_bittbl();
	Lck_New(&hcb_mtx);
}

static int
hcb_deref(struct objhead *oh)
{
	int r;
	struct hcb_y *y;

	r = 1;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	Lck_Lock(&oh->mtx);
	if (--oh->refcnt == 0) {
		Lck_Lock(&hcb_mtx);
		hcb_delete(&hcb_root, oh);
		y = (void*)&oh->u;
		if (y->leaf[0] == 0 && y->leaf[1] == 0)
			r = 0;
		else {
			/* XXX: on waiting list */
		}
		Lck_Unlock(&hcb_mtx);
	}
	Lck_Unlock(&oh->mtx);
	if (0) {
		fprintf(stderr, "%s %d %d <%s>\n", __func__, __LINE__, r, oh->hash);
		dump(&hcb_root, stderr);
	}
	return (r);
}

static struct objhead *
hcb_lookup(const struct sess *sp, struct objhead *noh)
{
	struct objhead *oh;
	unsigned u;
	
	assert(params->hash_sha256);
	oh =  hcb_insert(&hcb_root, noh, 0);
	if (oh != NULL) {
		/* Assert that we didn't muck with the tree without lock */
		assert(oh != noh);
		Lck_Lock(&oh->mtx);
		u = oh->refcnt;
		if (u)
			oh->refcnt++;
		Lck_Unlock(&oh->mtx);
		if (u) {
			VSL_stats->hcb_nolock++;
			return (oh);
		}
	}

	/*
	 * Try again, holding lock and fully ready objhead, so that if
	 * somebody else beats us back, they do not get surprised.
	 */
	HSH_Copy(sp, noh);
	Lck_Lock(&hcb_mtx);
	oh =  hcb_insert(&hcb_root, noh, 1);
	if (oh == noh) {
		VSL_stats->hcb_insert++;
		if (0) {
			fprintf(stderr, "%s %d\n", __func__, __LINE__);
			dump(&hcb_root, stderr);
		}
	} else {
		free(noh->hash);
		noh->hash = NULL;
		noh->hashlen = 0;
		VSL_stats->hcb_lock++;
		if (0) {
			fprintf(stderr, "%s %d\n", __func__, __LINE__);
			dump(&hcb_root, stderr);
		}
	}
	Lck_Unlock(&hcb_mtx);
	return (oh);
}


struct hash_slinger hcb_slinger = {
	.magic  =       SLINGER_MAGIC,
	.name   =       "critbit",
	.start  =       hcb_start,
	.lookup =       hcb_lookup,
	.deref  =       hcb_deref,
};
