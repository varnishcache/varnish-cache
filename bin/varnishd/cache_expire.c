/*
 * $Id$
 *
 * Expiry of cached objects and execution of prefetcher
 */

#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "binary_heap.h"
#include "cache.h"

static pthread_t exp_thread;
static struct binheap *exp_heap;
static pthread_mutex_t exp_mtx;
static unsigned expearly = 30;
static TAILQ_HEAD(,object) exp_deathrow = TAILQ_HEAD_INITIALIZER(exp_deathrow);

/*--------------------------------------------------------------------*/

void
EXP_Insert(struct object *o)
{

	AZ(pthread_mutex_lock(&exp_mtx));
	binheap_insert(exp_heap, o);
	AZ(pthread_mutex_unlock(&exp_mtx));
}

void
EXP_TTLchange(struct object *o)
{
	AZ(pthread_mutex_lock(&exp_mtx));
	binheap_delete(exp_heap, o->heap_idx);
	binheap_insert(exp_heap, o);
	AZ(pthread_mutex_unlock(&exp_mtx));
}

/*--------------------------------------------------------------------
 * This thread monitors deathrow and kills objects when they time out.
 */

static void *
exp_hangman(void *arg)
{
	struct object *o;
	time_t t;

	while (1) {
		time (&t); 
		AZ(pthread_mutex_lock(&exp_mtx));
		TAILQ_FOREACH(o, &exp_deathrow, deathrow) {
			if (o->ttl >= t)
				break;
			if (o->busy) {
				VSL(SLT_Debug, 0,
				    "Grim Reaper: Busy object xid %u", o->xid);
				continue;
			}
			if (o->refcnt == 0)
				break;
		}
		if (o == NULL || o->ttl >= t || o->refcnt > 0) {
			AZ(pthread_mutex_unlock(&exp_mtx));
			sleep(1);
			continue;
		}
		TAILQ_REMOVE(&exp_deathrow, o, deathrow);
		AZ(pthread_mutex_unlock(&exp_mtx));
		VSL(SLT_ExpKill, 0, "%u", o->xid);
		HSH_Deref(o);
	}
}

/*--------------------------------------------------------------------
 * This thread monitors the root of the binary heap and whenever an
 * object gets close enough, VCL is asked to decide if it should be
 * discarded or prefetched.
 * If discarded, the object is put on deathrow where exp_hangman() will
 * do what needs to be done.
 * XXX: If prefetched pass to the pool for pickup.
 */

static void *
exp_prefetch(void *arg)
{
	struct object *o;
	time_t t;
	struct sess sp;

	while (1) {
		time(&t);
		AZ(pthread_mutex_lock(&exp_mtx));
		o = binheap_root(exp_heap);
		if (o == NULL || o->ttl > t + expearly) {
			AZ(pthread_mutex_unlock(&exp_mtx));
			sleep(1);
			continue;
		}
		binheap_delete(exp_heap, 0);
		AZ(pthread_mutex_unlock(&exp_mtx));
		VSL(SLT_ExpPick, 0, "%u", o->xid);

		sp.vcl = GetVCL();
		sp.obj = o;
		VCL_timeout_method(&sp);
		RelVCL(sp.vcl);

		if (sp.handling == VCL_RET_DISCARD) {
			AZ(pthread_mutex_lock(&exp_mtx));
			TAILQ_INSERT_TAIL(&exp_deathrow, o, deathrow);
			AZ(pthread_mutex_unlock(&exp_mtx));
			continue;
		}
		assert(sp.handling == VCL_RET_DISCARD);
	}

	return ("FOOBAR");
}

/*--------------------------------------------------------------------*/

static int
object_cmp(void *priv, void *a, void *b)
{
	struct object *aa, *bb;

	aa = a;
	bb = b;
	return (aa->ttl < bb->ttl);
}

static void
object_update(void *priv, void *p, unsigned u)
{
	struct object *o = p;

	o->heap_idx = u;
}

/*--------------------------------------------------------------------*/

void
EXP_Init(void)
{

	AZ(pthread_mutex_init(&exp_mtx, NULL));
	exp_heap = binheap_new(NULL, object_cmp, object_update);
	assert(exp_heap != NULL);
	AZ(pthread_create(&exp_thread, NULL, exp_prefetch, NULL));
	AZ(pthread_create(&exp_thread, NULL, exp_hangman, NULL));
}
