/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "heritage.h"
#include "cache.h"

static struct hash_slinger      *hash;

struct object *
HSH_Lookup(struct worker *w, struct http *h)
{
	struct objhead *oh;
	struct object *o;
	char *b;

	assert(hash != NULL);
	/* Precreate an objhead and object in case we need them */
	if (w->nobjhead == NULL) {
		w->nobjhead = calloc(sizeof *w->nobjhead, 1);
		assert(w->nobjhead != NULL);
		TAILQ_INIT(&w->nobjhead->objects);
		AZ(pthread_mutex_init(&w->nobjhead->mtx, NULL));
		VSL_stats->n_objecthead++;
	}
	if (w->nobj == NULL) {
		w->nobj = calloc(sizeof *w->nobj, 1);
		assert(w->nobj != NULL);
		w->nobj->busy = 1;
		TAILQ_INIT(&w->nobj->store);
		AZ(pthread_cond_init(&w->nobj->cv, NULL));
		VSL_stats->n_object++;
	}

	assert(http_GetURL(h, &b));
	oh = hash->lookup(b, w->nobjhead);
	if (oh == w->nobjhead)
		w->nobjhead = NULL;
	AZ(pthread_mutex_lock(&oh->mtx));
	TAILQ_FOREACH(o, &oh->objects, list) {
		o->refcnt++;
		if (o->busy)
			AZ(pthread_cond_wait(&o->cv, &oh->mtx));
		/* XXX: check TTL */
		if (o->ttl == 0) {
			/* Object banned but not reaped yet */
		} else if (BAN_CheckObject(o, b)) {
			o->ttl = 0;
			VSL(SLT_ExpBan, 0, "%u was banned", o->xid);
			EXP_TTLchange(o);
		} else 
			break;
		o->refcnt--;
	}
	if (o != NULL) {
		AZ(pthread_mutex_unlock(&oh->mtx));
		hash->deref(oh);
		return (o);
	}

	/* Insert (precreated) object in objecthead */
	o = w->nobj;
	w->nobj = NULL;
	o->refcnt = 1;
	o->objhead = oh;
	TAILQ_INSERT_TAIL(&oh->objects, o, list);
	/* NB: do not deref objhead the new object inherits our reference */
	AZ(pthread_mutex_unlock(&oh->mtx));
	BAN_NewObj(o);
	return (o);
}

void
HSH_Unbusy(struct object *o)
{

	AZ(pthread_mutex_lock(&o->objhead->mtx));
	o->busy = 0;
	AZ(pthread_mutex_unlock(&o->objhead->mtx));
	AZ(pthread_cond_broadcast(&o->cv));
}

void
HSH_Deref(struct object *o)
{
	struct objhead *oh;
	struct storage *st, *stn;

	oh = o->objhead;

	/* drop ref on object */
	AZ(pthread_mutex_lock(&oh->mtx));
	if (--o->refcnt == 0)
		TAILQ_REMOVE(&oh->objects, o, list);
	else 
		o = NULL;
	AZ(pthread_mutex_unlock(&oh->mtx));

	/* If still referenced, done */
	if (o == NULL)
		return;

	if (o->header != NULL) {
		free(o->header);
		VSL_stats->n_header--;
	}
	AZ(pthread_cond_destroy(&o->cv));

	TAILQ_FOREACH_SAFE(st, &o->store, list, stn) {
		TAILQ_REMOVE(&o->store, st, list);
		st->stevedore->free(st);
	}
	free(o);
	VSL_stats->n_object--;

	/* Drop our ref on the objhead */
	if (hash->deref(oh))
		return;
	assert(TAILQ_EMPTY(&oh->objects));
	AZ(pthread_mutex_destroy(&oh->mtx));
	VSL_stats->n_objecthead--;
	free(oh);
}

void
HSH_Init(void)
{

	hash = heritage.hash;
	if (hash->start != NULL)
		hash->start();
}
