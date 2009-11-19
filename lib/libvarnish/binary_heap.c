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

/* Paramters ---------------------------------------------------------*/

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
};

#define PARENT(u)	((u) / 2)
/*lint -emacro(835, CHILD) 0 right of + */
#define CHILD(u,n)	((u) * 2 + (n))

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

	bh = calloc(sizeof *bh, 1);
	if (bh == NULL)
		return (bh);
	bh->priv = priv;
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
		v = PARENT(u);
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
		v1 = CHILD(u, 0);
		if (v1 >= bh->next)
			return;
		v2 = CHILD(u, 1);
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

void *
binheap_root(const struct binheap *bh)
{

	assert(bh != NULL);
	assert(bh->magic == BINHEAP_MAGIC);
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


#ifdef TEST_DRIVER
/* Test driver -------------------------------------------------------*/
#include <stdio.h>

#if 1

#define N 23

static int
cmp(void *priv, void *a, void *b)
{

	return (*(unsigned *)a < *(unsigned *)b);
}

void
update(void *priv, void *a, unsigned u)
{
	printf("%p is now %u\n", a, u);
}

static void
dump(struct binheap *bh, const char *what, unsigned ptr)
{
	FILE *f;
	unsigned u, *up;

	printf("dump\n");
	f = popen("dot -Tps >> /tmp/_.ps 2>/dev/null", "w");
	assert(f != NULL);
	fprintf(f, "digraph binheap {\n");
	fprintf(f, "size=\"7,10\"\n");
	fprintf(f, "ptr [label=\"%s\"]\n", what);
	fprintf(f, "ptr -> node_%u\n", ptr);
	for (u = 1; u < bh->next; u++) {
		up = A(bh, u);
		fprintf(f, "node_%u [label=\"%u\"];\n", u, *up);
		if (u > 0)
			fprintf(f, "node_%u -> node_%u\n", PARENT(u), u);
	}
	fprintf(f, "}\n");
	pclose(f);
}

int
main(int argc, char **argv)
{
	struct binheap *bh;
	unsigned l[N], u, *up, lu;

	system("echo %! > /tmp/_.ps");
	bh = binheap_new(NULL, cmp, update);
	for (u = 0; u < N; u++) {
		l[u] = random() % 1000;
		binheap_insert(bh, &l[u]);
		if (1)
			dump(bh, "Insert", 0);
	}
	printf("Inserts done\n");
	lu = 0;
	while(1) {
		up = binheap_root(bh);
		if (up == NULL)
			break;
		assert(*up >= lu);
		lu = *up;
		u = (random() % (bh->next - 1)) + 1;
		binheap_delete(bh, u);
		if (1)
			dump(bh, "Delete", u);
	}
	printf("Deletes done\n");
	return (0);
}
#else
struct foo {
	unsigned	idx;
	unsigned	key;
};

#define M 1311191
#define N 1311

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
	unsigned u, v, nb = 0;
	struct foo *fa, *fb;

	for (u = 2; u < bh->next; u++) {
		v = PARENT(u);
		fa = A(bh, u);
		fb = A(bh, v);
		assert(fa->key > fb->key);
		continue;
		printf("[%2u/%2u] %10u > [%2u/%2u] %10u %s\n",
		    u, fa - ff, fa->key,
		    v, fb - ff, fb->key,
		    fa->key > fb->key ? "OK" : "BAD");
		if (fa->key <= fb->key)
			nb++;
	}
	if (nb)
		exit(0);
}

int
main(int argc, char **argv)
{
	struct binheap *bh;
	unsigned u, v;

	if (0) {
		srandomdev();
		u = random();
		printf("Seed %u\n", u);
		srandom(u);
	}
	bh = binheap_new(NULL, cmp, update);
	for (u = 0; u < M; u++) {
		v = random() % N;
		if (ff[v].idx > 0) {
			if (0)
				printf("Delete [%u] %'u\n", v, ff[v].key);
			else
				printf("-%u", v);
			binheap_delete(bh, ff[v].idx);
		} else {
			ff[v].key = random();
			if (0)
				printf("Insert [%u] %'u\n", v, ff[v].key);
			else
				printf("+%u", v);
			binheap_insert(bh, &ff[v]);
		}
		chk(bh);
	}
	return (0);
}
#endif
#endif
