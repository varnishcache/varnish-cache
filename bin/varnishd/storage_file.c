/*
 * $Id: storage_malloc.c 170 2006-06-13 07:57:32Z phk $
 *
 * Storage method based on mmap'ed file
 */

#include <assert.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <pthread.h>

#include "vcl_lang.h"
#include "cache.h"

struct smf {
	struct storage		s;
};

static struct storage *
smf_alloc(struct stevedore *st __unused, unsigned size)
{
	struct smf *smf;

	smf = calloc(sizeof *smf, 1);
	assert(smf != NULL);
	smf->s.priv = smf;
	smf->s.ptr = malloc(size);
	assert(smf->s.ptr != NULL);
	smf->s.len = size;
	return (&smf->s);
}

static void
smf_free(struct storage *s)
{
	struct smf *smf;

	smf = s->priv;
	free(smf->s.ptr);
	free(smf);
}

struct stevedore smf_stevedore = {
	"file",
	NULL,			/* init */
	NULL,			/* open */
	smf_alloc,
	smf_free
};
