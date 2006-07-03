/*
 * $Id$
 *
 * Storage method based on malloc(3)
 */

#include <assert.h>
#include <stdlib.h>
#include <queue.h>
#include <pthread.h>

#include "cache.h"

struct sma {
	struct storage		s;
};

static struct storage *
sma_alloc(struct stevedore *st, size_t size)
{
	struct sma *sma;

	sma = calloc(sizeof *sma, 1);
	assert(sma != NULL);
	sma->s.priv = sma;
	sma->s.ptr = malloc(size);
	assert(sma->s.ptr != NULL);
	sma->s.len = 0;
	sma->s.space = size;
	sma->s.stevedore = st;
	return (&sma->s);
}

static void
sma_free(struct storage *s)
{
	struct sma *sma;

	sma = s->priv;
	free(sma->s.ptr);
	free(sma);
}

struct stevedore sma_stevedore = {
	"malloc",
	NULL,			/* init */
	NULL,			/* open */
	sma_alloc,
	NULL,			/* trim */
	sma_free
};
