/*
 * $Id$
 *
 * Storage method based on malloc(3)
 */

#include <assert.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <pthread.h>

#include "vcl_lang.h"
#include "cache.h"

struct sma {
	struct storage		s;
};

static struct storage *
sma_alloc(unsigned size)
{
	struct sma *sma;

	sma = calloc(sizeof *sma, 1);
	assert(sma != NULL);
	sma->s.priv = sma;
	sma->s.ptr = malloc(size);
	assert(sma->s.ptr != NULL);
	sma->s.len = size;
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
	"Malloc",
	NULL,			/* init */
	sma_alloc,
	sma_free
};
