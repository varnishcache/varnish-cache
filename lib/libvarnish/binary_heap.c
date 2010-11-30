/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
 * Implementation of a binary heap API
 *
 * We use a malloc(3)/realloc(3) array to store the pointers using the
 * classical FORTRAN strategy.
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <unistd.h>
#include <stdlib.h>

#include "binary_heap.h"
#include "libvarnish.h"

/* Parameters --------------------------------------------------------*/

/*
 * The number of elements in a row has to be a compromise between
 * wasted space and number of memory allocations.
 * With 64k objects per row, there will be at least 5...10 seconds
 * between row additions on a very busy server.
 * At the same time, the worst case amount of wasted memory is kept
 * at a reasonable 1 MB -- two rows on 64bit system.
 * Finally, but without practical significance: 16 bits should be
 * easier for the compiler to optimize.
 */
#define ROW_SHIFT		16


#undef PARANOIA

/* Private definitions -----------------------------------------------*/

#define ROOT_IDX		1

#define ROW_WIDTH		(1 << ROW_SHIFT)

/*lint -emacro(572, ROW) shift 0 >> by 16 */
/*lint -emacro(835, ROW) 0 left of >> */
/*lint -emacro(778, ROW) const >> evaluates to zero */
#define ROW(b, n)		((b)->array[(n) >> ROW_SHIFT])

/*lint -emacro(835, A) 0 left of & */
#define A(b, n)			ROW(b, n)[(n) & (ROW_WIDTH - 1)]

struct binheap {
	unsigned		magic;
#define BINHEAP_MAGIC		0xf581581aU	/* from /dev/random */
	void			*priv;
	binheap_cmp_t		*cmp;
	binheap_update_t	*update;
	void			***array;
	unsigned		rows;
	unsigned		length;
	unsigned		next;
	unsigned		page_size;
	unsigned		page_mask;
	unsigned		page_shift;
};

#define VM_AWARE

#ifdef VM_AWARE

static  unsigned
parent(const struct binheap *bh, unsigned u)
{
	unsigned po;
	unsigned v;

	po = u & bh->page_mask;

	if (u < bh->page_size || po > 3) {
		v = (u & ~bh->page_mask) | (po >> 1);
	} else if (po < 2) {
		v = (u - bh->page_size) >> bh->page_shift;
		v += v & ~(bh->page_mask >> 1);
		v |= bh->page_size / 2;
	} else {
		v = u - 2;
	}
	return (v);
}

static void
child(const struct binheap *bh, unsigned u, unsigned *a, unsigned *b)
{

	if (u > bh->page_mask && (u & (bh->page_mask - 1)) == 0) {
		/* First two elements are magical except on the first page */
		*a = *b = u + 2;
	} else if (u & (bh->page_size >> 1)) {
		/* The bottom row is even more magical */
		*a = (u & ~bh->page_mask) >> 1;
		*a |= u & (bh->page_mask >> 1);
		*a += 1;
		*a <<= bh->page_shift;
		*b = *a + 1;
	} else {
		/* The rest is as usual, only inside the page */
		*a = u + (u & bh->page_mask);
		*b = *a + 1;
	}
#ifdef PARANOIA
	assert(parent(bh, *a) == u);
	assert(parent(bh, *b) == u);
#endif
}


#else

static unsigned
parent(const struct binheap *bh, unsigned u)
{

	(void)bh;
	return (u / 2);
}

static void
child(const struct binheap *bh, unsigned u, unsigned *a, unsigned *b)
{

	(void)bh;
	*a = u * 2;
	*b = *a + 1;
}

#endif

/* Implementation ----------------------------------------------------*/

static void
binheap_addrow(struct binheap *bh)
{
	unsigned u;

	/* First make sure we have space for another row */
	if (&ROW(bh, bh->length) >= bh->array + bh->rows) {
		u = bh->rows * 2;
		bh->array = realloc(bh->array, sizeof(*bh->array) * u);
		assert(bh->array != NULL);

		/* NULL out new pointers */
		while (bh->rows < u)
			bh->array[bh->rows++] = NULL;
	}
	assert(ROW(bh, bh->length) == NULL);
	ROW(bh, bh->length) = malloc(sizeof(**bh->array) * ROW_WIDTH);
	assert(ROW(bh, bh->length));
	bh->length += ROW_WIDTH;
}

struct binheap *
binheap_new(void *priv, binheap_cmp_t *cmp_f, binheap_update_t *update_f)
{
	struct binheap *bh;
	unsigned u;

	bh = calloc(sizeof *bh, 1);
	if (bh == NULL)
		return (bh);
	bh->priv = priv;

	bh->page_size = (unsigned)getpagesize() / sizeof (void *);
	bh->page_mask = bh->page_size - 1;
	assert(!(bh->page_size & bh->page_mask));	/* power of two */
	for (u = 1; (1U << u) != bh->page_size; u++)
		;
	bh->page_shift = u;
	assert(bh->page_size <= (sizeof(**bh->array) * ROW_WIDTH));
	
	bh->cmp = cmp_f;
	bh->update = update_f;
	bh->next = ROOT_IDX;
	bh->rows = 16;		/* A tiny-ish number */
	bh->array = calloc(sizeof *bh->array, bh->rows);
	assert(bh->array != NULL);
	binheap_addrow(bh);
	A(bh, ROOT_IDX) = NULL;
	bh->magic = BINHEAP_MAGIC;
	return (bh);
}

static void
binheap_update(const struct binheap *bh, unsigned u)
{
	assert(bh->magic == BINHEAP_MAGIC);
	assert(u < bh->next);
	assert(A(bh, u) != NULL);
	if (bh->update == NULL)
		return;
	bh->update(bh->priv, A(bh, u), u);
}

static void
binhead_swap(const struct binheap *bh, unsigned u, unsigned v)
{
	void *p;

	assert(bh->magic == BINHEAP_MAGIC);
	assert(u < bh->next);
	assert(v < bh->next);
	p = A(bh, u);
	A(bh, u) = A(bh, v);
	A(bh, v) = p;
	binheap_update(bh, u);
	binheap_update(bh, v);
}

static unsigned
binheap_trickleup(const struct binheap *bh, unsigned u)
{
	unsigned v;

	assert(bh->magic == BINHEAP_MAGIC);
	while (u > ROOT_IDX) {
		v = parent(bh, u);
		if (!bh->cmp(bh->priv, A(bh, u), A(bh, v)))
			break;
		binhead_swap(bh, u, v);
		u = v;
	}
	return (u);
}

static void
binheap_trickledown(const struct binheap *bh, unsigned u)
{
	unsigned v1, v2;

	assert(bh->magic == BINHEAP_MAGIC);
	while (1) {
		child(bh, u, &v1, &v2);
		if (v1 >= bh->next)
			return;
		if (v2 < bh->next && bh->cmp(bh->priv, A(bh, v2), A(bh, v1)))
			v1 = v2;
		if (bh->cmp(bh->priv, A(bh, u), A(bh, v1)))
			return;
		binhead_swap(bh, u, v1);
		u = v1;
	}
}

void
binheap_insert(struct binheap *bh, void *p)
{
	unsigned u;

	assert(bh != NULL);
	assert(bh->magic == BINHEAP_MAGIC);
	assert(bh->length >= bh->next);
	if (bh->length == bh->next)
		binheap_addrow(bh);
	u = bh->next++;
	A(bh, u) = p;
	binheap_update(bh, u);
	(void)binheap_trickleup(bh, u);
}


#ifdef PARANOIA
static void
chk(const struct binheap *bh)
{
	unsigned u, v;

	for (u = 2; u < bh->next; u++) {
		v = parent(bh, u);
		assert(!bh->cmp(bh->priv, A(bh, u), A(bh, v)));
	}
}
#endif 

void *
binheap_root(const struct binheap *bh)
{

	assert(bh != NULL);
	assert(bh->magic == BINHEAP_MAGIC);
#ifdef PARANOIA
	chk(bh);
#endif
	return (A(bh, ROOT_IDX));
}

/*
 * It may seem counter-intuitive that we delete by replacement with
 * the tail object. "That's almost certain to not belong there, in
 * particular when we delete the root ?" is the typical reaction.
 *
 * If we tried to trickle up into the empty position, we would,
 * eventually, end up with a hole in the bottom row, at which point
 * we would move the tail object there.
 * But there is no guarantee that the tail object would not need to
 * trickle up from that position, in fact, it might be the new root
 * of this half of the subtree.
 * The total number of operations is guaranteed to be at least
 * N{height} downward selections, because we have to get the hole
 * all the way down, but in addition to that, we may get up to
 * N{height}-1 upward trickles.
 *
 * When we fill the hole with the tail object, the worst case is
 * that it trickles all the way down to become the tail object
 * again.
 * In other words worst case is N{height} downward trickles.
 * But there is a pretty decent chance that it does not make
 * it all the way down.
 */

void
binheap_delete(struct binheap *bh, unsigned idx)
{

	assert(bh != NULL);
	assert(bh->magic == BINHEAP_MAGIC);
	assert(bh->next > ROOT_IDX);
	assert(idx < bh->next);
	assert(idx > 0);
	assert(A(bh, idx) != NULL);
	bh->update(bh->priv, A(bh, idx), 0);
	if (idx == --bh->next) {
		A(bh, bh->next) = NULL;
		return;
	}
	A(bh, idx) = A(bh, bh->next);
	A(bh, bh->next) = NULL;
	binheap_update(bh, idx);
	idx = binheap_trickleup(bh, idx);
	binheap_trickledown(bh, idx);

	/*
	 * We keep a hysteresis of one full row before we start to
	 * return space to the OS to avoid silly behaviour around
	 * row boundaries.
	 */
	if (bh->next + 2 * ROW_WIDTH <= bh->length) {
		free(ROW(bh, bh->length - 1));
		ROW(bh, bh->length - 1) = NULL;
		bh->length -= ROW_WIDTH;
	}
}

/*
 * Move an item up/down after changing its key value
 */

void
binheap_reorder(struct binheap *bh, unsigned idx)
{

	assert(bh != NULL);
	assert(bh->magic == BINHEAP_MAGIC);
	assert(bh->next > ROOT_IDX);
	assert(idx < bh->next);
	assert(idx > 0);
	assert(A(bh, idx) != NULL);
	idx = binheap_trickleup(bh, idx);
	binheap_trickledown(bh, idx);
}

#ifdef TEST_DRIVER
/* Test driver -------------------------------------------------------*/
#include <stdio.h>

struct foo {
	unsigned	idx;
	unsigned	key;
};

#define M 31011091	/* Number of operations */
#define N 10313102		/* Number of items */
#define R -1		/* Random modulus */

struct foo ff[N];

static int
cmp(void *priv, void *a, void *b)
{
	struct foo *fa, *fb;

	fa = a;
	fb = b;
	return (fa->key < fb->key);
}

void
update(void *priv, void *a, unsigned u)
{
	struct foo *fa;

	fa = a;
	fa->idx = u;
}

void
chk(struct binheap *bh)
{
	unsigned u, v;
	struct foo *fa, *fb;

	for (u = 2; u < bh->next; u++) {
		v = parent(bh, u);
		fa = A(bh, u);
		fb = A(bh, v);
		assert(fa->key >= fb->key);
	}
}

int
main(int argc, char **argv)
{
	struct binheap *bh;
	unsigned u, v, lr;
	struct foo *fp;

	if (0) {
		srandomdev();
		u = random();
		printf("Seed %u\n", u);
		srandom(u);
	}
	bh = binheap_new(NULL, cmp, update);

	/* First insert our N elements */
	for (u = 0; u < N; u++) {
		lr = random() % R;
		ff[u].key = lr;
		binheap_insert(bh, &ff[u]);

		fp = binheap_root(bh);
		assert(fp->idx == 1);
		assert(fp->key <= lr);
	}
	fprintf(stderr, "%d inserts OK\n", N);
	/* For M cycles, pick the root, insert new */
	for (u = 0; u < M; u++) {
		fp = binheap_root(bh);
		assert(fp->idx == 1);

		/* It cannot possibly be larger than the last value we added */
		assert(fp->key <= lr);
		binheap_delete(bh, fp->idx);

		lr = random() % R;
		fp->key = lr;
		binheap_insert(bh, fp);
	}
	fprintf(stderr, "%d replacements OK\n", M);
	/* The remove everything */
	lr = 0;
	for (u = 0; u < N; u++) {
		fp = binheap_root(bh);
		assert(fp->idx == 1);
		assert(fp->key >= lr);
		lr = fp->key;
		binheap_delete(bh, fp->idx);
	}
	fprintf(stderr, "%d removes OK\n", N);

	for (u = 0; u < M; u++) {
		v = random() % N;
		if (ff[v].idx > 0) {
			assert(ff[v].idx != 0);
			binheap_delete(bh, ff[v].idx);
			assert(ff[v].idx == 0);
		} else {
			assert(ff[v].idx == 0);
			ff[v].key = random() % R;
			binheap_insert(bh, &ff[v]);
			assert(ff[v].idx != 0);
		}
		if (0)
			chk(bh);
	}
	fprintf(stderr, "%d updates OK\n", M);
	return (0);
}
#endif
