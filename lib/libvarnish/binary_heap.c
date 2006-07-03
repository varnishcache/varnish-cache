/*
 * $Id$
 *
 * Implementation of a binary heap API
 *
 * We use a malloc(3)/realloc(3) array to store the pointers using the
 * classical FORTRAN strategy.
 *
 * XXX: the array is not scaled back when items are deleted.
 */

#include <assert.h>
#include <unistd.h>
#include <stdlib.h>

#include "binary_heap.h"

/* Private definitions -----------------------------------------------*/

#define MIN_LENGTH		16

struct binheap {
	unsigned		magic;
#define BINHEAP_MAGIC		0xf581581aU	/* from /dev/random */
	void			*priv;
	binheap_cmp_t		*cmp;
	binheap_update_t	*update;
	void			**array;
	unsigned		length;
	unsigned		next;
	unsigned		granularity;
};

#define PARENT(u)	(((u) - 1) / 2)
#define CHILD(u,n)	((u) * 2 + (n))

/* Implementation ----------------------------------------------------*/

static void
binheap_update(struct binheap *bh, unsigned u)
{
	assert(bh->magic == BINHEAP_MAGIC);
	assert(u < bh->next);
	if (bh->update == NULL)
		return;
	bh->update(bh->priv, bh->array[u], u);
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
	bh->next = 0;
	bh->length = MIN_LENGTH;
	bh->array = calloc(sizeof *bh->array, bh->length);
	assert(bh->array != NULL);
	bh->granularity = getpagesize() / sizeof *bh->array;
	bh->magic = BINHEAP_MAGIC;
	return (bh);
}

static void
binhead_swap(struct binheap *bh, unsigned u, unsigned v)
{
	void *p;

	assert(bh->magic == BINHEAP_MAGIC);
	assert(u < bh->next);
	assert(v < bh->next);
	p = bh->array[u];
	bh->array[u] = bh->array[v];
	bh->array[v] = p;
	binheap_update(bh, u);
	binheap_update(bh, v);
}

static unsigned
binheap_trickleup(struct binheap *bh, unsigned u)
{
	unsigned v;

	assert(bh->magic == BINHEAP_MAGIC);
	while (u > 0) {
		v = PARENT(u);
		if (bh->cmp(bh->priv, bh->array[u], bh->array[v])) {
			binhead_swap(bh, u, v);
			u = v;
		} else
			break;
	}
	return (u);
}

static void
binheap_trickledown(struct binheap *bh, unsigned u)
{
	unsigned v1, v2;

	assert(bh->magic == BINHEAP_MAGIC);
	while (1) {
		v1 = CHILD(u, 1);
		if (v1 >= bh->next)
			return;
		v2 = CHILD(u, 2);
		if (v2 >= bh->next) {
			if (!bh->cmp(bh->priv, bh->array[u], bh->array[v1]))
				binhead_swap(bh, u, v1);
			return;
		}
		if (bh->cmp(bh->priv, bh->array[v1], bh->array[v2])) {
			if (!bh->cmp(bh->priv, bh->array[u], bh->array[v1])) {
				binhead_swap(bh, u, v1);
				u = v1;
				continue;
			}
		} else {
			if (!bh->cmp(bh->priv, bh->array[u], bh->array[v2])) {
				binhead_swap(bh, u, v2);
				u = v2;
				continue;
			}
		}
		return;
	}
}

void
binheap_insert(struct binheap *bh, void *p)
{
	unsigned u;

	assert(bh != NULL);
	assert(bh->magic == BINHEAP_MAGIC);
	assert(bh->length >= bh->next);
	if (bh->length == bh->next) {
		if (bh->length >= bh->granularity * 32)
			bh->length += bh->granularity * 32;
		else if (bh->length > bh->granularity)
			bh->length += bh->granularity;
		else
			bh->length += bh->length;
		bh->array = realloc(bh->array, bh->length * sizeof *bh->array);
		assert(bh->array != NULL);
	}
	u = bh->next++;
	bh->array[u] = p;
	binheap_update(bh, u);
	binheap_trickleup(bh, u);
}

void *
binheap_root(struct binheap *bh)
{

	assert(bh != NULL);
	assert(bh->magic == BINHEAP_MAGIC);
	if(bh->next == 0)
		return (NULL);
	return (bh->array[0]);
}

void
binheap_delete(struct binheap *bh, unsigned idx)
{

	assert(bh != NULL);
	assert(bh->magic == BINHEAP_MAGIC);
	assert(bh->next > 0);
	assert(idx < bh->next);
	if (idx == --bh->next)
		return;
	bh->array[idx] = bh->array[bh->next];
	binheap_update(bh, idx);
	binheap_trickledown(bh, idx);
	/* XXX: free part of array ? */
}


#ifdef TEST_DRIVER
/* Test driver -------------------------------------------------------*/

#include <stdio.h>

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
	f = popen("dot -Tps >> /tmp/_.ps", "w");
	assert(f != NULL);
	fprintf(f, "digraph binheap {\n");
	fprintf(f, "size=\"7,10\"\n");
	fprintf(f, "ptr [label=\"%s\"]\n", what);
	fprintf(f, "ptr -> node_%u\n", ptr);
	for (u = 0; u < bh->next; u++) {
		up = bh->array[u];
		fprintf(f, "node_%u [label=\"%u\"];\n", u, *up);
		if (u > 0)
			fprintf(f, "node_%u -> node_%u\n", PARENT(u), u);
	}
	fprintf(f, "}\n");
	pclose(f);
}

#define N 31
int
main(int argc, char **argv)
{
	struct binheap *bh;
	unsigned l[N], u, *up, lu;

	srandomdev();
	u = random();
	printf("Seed %u\n", u);
	srandom(u);
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
		u = random() % bh->next;
		binheap_delete(bh, u);
		if (1)
			dump(bh, "Delete", u);
	}
	printf("Deletes done\n");

	return (0);
}
#endif
