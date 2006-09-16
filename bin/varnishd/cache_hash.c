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
	char *url, *host;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(sp->http, HTTP_MAGIC);
	AN(hash);
	w = sp->wrk;
	h = sp->http;

	/* Precreate an objhead and object in case we need them */
	if (w->nobjhead == NULL) {
		w->nobjhead = calloc(sizeof *w->nobjhead, 1);
		XXXAN(w->nobjhead);
		w->nobjhead->magic = OBJHEAD_MAGIC;
		TAILQ_INIT(&w->nobjhead->objects);
		MTX_INIT(&w->nobjhead->mtx);
		VSL_stats->n_objecthead++;
	} else
		CHECK_OBJ_NOTNULL(w->nobjhead, OBJHEAD_MAGIC);
	if (w->nobj == NULL) {
		w->nobj = calloc(sizeof *w->nobj, 1);
		XXXAN(w->nobj);
		w->nobj->magic = OBJECT_MAGIC;
		w->nobj->http.magic = HTTP_MAGIC;
		w->nobj->busy = 1;
		w->nobj->refcnt = 1;
		TAILQ_INIT(&w->nobj->store);
		TAILQ_INIT(&w->nobj->waitinglist);
		VSL_stats->n_object++;
	} else
		CHECK_OBJ_NOTNULL(w->nobj, OBJECT_MAGIC);

	url = h->hd[HTTP_HDR_URL].b;
	if (!http_GetHdr(h, H_Host, &host))
		host = url;
	if (sp->obj != NULL) {
		CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
		o = sp->obj;
		oh = o->objhead;
		CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
		LOCK(&oh->mtx);
		goto were_back;
	}
	oh = hash->lookup(url, host, w->nobjhead);
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	if (oh == w->nobjhead)
		w->nobjhead = NULL;
	LOCK(&oh->mtx);
	TAILQ_FOREACH(o, &oh->objects, list) {
		o->refcnt++;
		if (o->busy) {
			TAILQ_INSERT_TAIL(&o->waitinglist, sp, list);
			sp->obj = o;
			UNLOCK(&oh->mtx);
			return (NULL);
		}
	were_back:
		/* XXX: check Vary: */
		if (!o->cacheable) {
			/* ignore */
		} else if (o->ttl == 0) {
			/* Object banned but not reaped yet */
		} else if (o->ttl <= sp->t_req.tv_sec) {
			/* Object expired */
		} else if (BAN_CheckObject(o, url)) {
			o->ttl = 0;
			VSL(SLT_ExpBan, 0, "%u was banned", o->xid);
			EXP_TTLchange(o);
		} else 
			break;
		o->refcnt--;
	}
	if (o != NULL) {
		UNLOCK(&oh->mtx);
		(void)hash->deref(oh);
		return (o);
	}

	/* Insert (precreated) object in objecthead */
	o = w->nobj;
	w->nobj = NULL;
	o->objhead = oh;
	TAILQ_INSERT_TAIL(&oh->objects, o, list);
	/* NB: do not deref objhead the new object inherits our reference */
	UNLOCK(&oh->mtx);
	BAN_NewObj(o);
	return (o);
}

void
HSH_Unbusy(struct object *o)
{
	struct sess *sp;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	assert(o->busy);
	assert(o->refcnt > 0);
	if (o->cacheable)
		EXP_Insert(o);
	LOCK(&o->objhead->mtx);
	o->busy = 0;
	UNLOCK(&o->objhead->mtx);
	while (1) {
		sp = TAILQ_FIRST(&o->waitinglist);
		if (sp == NULL)
			break;
		TAILQ_REMOVE(&o->waitinglist, sp, list);
		WRK_QueueSession(sp);
	}
}

void
HSH_Ref(struct object *o)
{
	struct objhead *oh;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oh = o->objhead;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	LOCK(&oh->mtx);
	assert(o->refcnt > 0);
	o->refcnt++;
	UNLOCK(&oh->mtx);
}

void
HSH_Deref(struct object *o)
{
	struct objhead *oh;
	struct storage *st, *stn;
	unsigned r;

	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	oh = o->objhead;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);

	/* drop ref on object */
	LOCK(&oh->mtx);
	assert(o->refcnt > 0);
	r = --o->refcnt;
	if (!r)
		TAILQ_REMOVE(&oh->objects, o, list);
	UNLOCK(&oh->mtx);

	/* If still referenced, done */
	if (r != 0)
		return;

	if (o->http.s != NULL) {
		free(o->http.s);
	}

	TAILQ_FOREACH_SAFE(st, &o->store, list, stn) {
		CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
		TAILQ_REMOVE(&o->store, st, list);
		st->stevedore->free(st);
	}
	free(o);
	VSL_stats->n_object--;

	/* Drop our ref on the objhead */
	if (hash->deref(oh))
		return;
	assert(TAILQ_EMPTY(&oh->objects));
	MTX_DESTROY(&oh->mtx);
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
