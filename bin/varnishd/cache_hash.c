/*
 * $Id$
 *
 * This is the central hash-table code, it relies on a chosen hash 
 * implmentation only for the actual hashing, all the housekeeping
 * happens here.
 *
 * We have two kinds of structures, objecthead and object.  An objecthead
 * corresponds to a given (Host:, URL) tupple, and the objects hung from
 * the objecthead may represent various variations (ie: Vary: header,
 * different TTL etc) instances of that web-entity.
 *
 * Each objecthead has a mutex which locks both its own fields, the
 * list of objects and fields in the objects.
 *   
 * The hash implementation must supply a reference count facility on
 * the objecthead, and return with a reference held after a lookup.
 *
 * Lookups in the hash implementation returns with a ref held and each
 * object hung from the objhead holds a ref as well.
 *
 * Objects have refcounts which are locked by the objecthead mutex.
 *
 * New objects are always marked busy, and they can go from busy to
 * not busy only once.
 *
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
HSH_Lookup(struct sess *sp)
{
	struct worker *w;
	struct http *h;
	struct objhead *oh;
	struct object *o;
	char *c;

	assert(hash != NULL);
	w = sp->wrk;
	h = sp->http;

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
		w->nobj->refcnt = 1;
		TAILQ_INIT(&w->nobj->store);
		TAILQ_INIT(&w->nobj->waitinglist);
		VSL_stats->n_object++;
	}

	if (!http_GetHdr(h, "Host", &c))
		c = h->url;
	if (sp->obj != NULL) {
		o = sp->obj;
		oh = o->objhead;
		AZ(pthread_mutex_lock(&oh->mtx));
		goto were_back;
	}
	oh = hash->lookup(h->url, c, w->nobjhead);
	if (oh == w->nobjhead)
		w->nobjhead = NULL;
	AZ(pthread_mutex_lock(&oh->mtx));
	TAILQ_FOREACH(o, &oh->objects, list) {
		o->refcnt++;
		if (o->busy) {
			TAILQ_INSERT_TAIL(&o->waitinglist, sp, list);
			sp->obj = o;
			AZ(pthread_mutex_unlock(&oh->mtx));
			return (NULL);
		}
	were_back:
		/* XXX: check ttl */
		/* XXX: check Vary: */
		if (!o->cacheable) {
			/* ignore */
		} else if (o->ttl == 0) {
			/* Object banned but not reaped yet */
		} else if (BAN_CheckObject(o, h->url)) {
			o->ttl = 0;
			VSL(SLT_ExpBan, 0, "%u was banned", o->xid);
			EXP_TTLchange(o);
		} else 
			break;
		o->refcnt--;
	}
	if (o != NULL) {
		AZ(pthread_mutex_unlock(&oh->mtx));
		(void)hash->deref(oh);
		return (o);
	}

	/* Insert (precreated) object in objecthead */
	o = w->nobj;
	w->nobj = NULL;
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
	struct sess *sp;

	assert(o != NULL);
	assert(o->refcnt > 0);
	if (o->cacheable)
		EXP_Insert(o);
	AZ(pthread_mutex_lock(&o->objhead->mtx));
	o->busy = 0;
	AZ(pthread_mutex_unlock(&o->objhead->mtx));
	while (1) {
		sp = TAILQ_FIRST(&o->waitinglist);
		if (sp == NULL)
			break;
		TAILQ_REMOVE(&o->waitinglist, sp, list);
		WRK_QueueSession(sp);
	}
}

void
HSH_Deref(struct object *o)
{
	struct objhead *oh;
	struct storage *st, *stn;
	unsigned r;

	oh = o->objhead;

	/* drop ref on object */
	AZ(pthread_mutex_lock(&oh->mtx));
	r = --o->refcnt;
	if (!r)
		TAILQ_REMOVE(&oh->objects, o, list);
	AZ(pthread_mutex_unlock(&oh->mtx));

	/* If still referenced, done */
	if (r != 0)
		return;

	if (o->header != NULL) {
		free(o->header);
		VSL_stats->n_header--;
	}

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
