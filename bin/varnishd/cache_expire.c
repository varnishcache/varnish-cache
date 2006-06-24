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
#include "binary_heap.h"
#include "cache.h"

static pthread_t exp_thread;
static struct binheap *exp_heap;
static pthread_mutex_t expmtx;
static unsigned expearly = 30;

/*--------------------------------------------------------------------*/

void
EXP_Insert(struct object *o)
{

	AZ(pthread_mutex_lock(&expmtx));
	binheap_insert(exp_heap, o);
	AZ(pthread_mutex_unlock(&expmtx));
}

/*--------------------------------------------------------------------*/

static void *
exp_main(void *arg)
{
	struct object *o;
	time_t t;

	while (1) {
		time(&t);
		AZ(pthread_mutex_lock(&expmtx));
		o = binheap_root(exp_heap);
		if (o == NULL || o->ttl - t > expearly) {
			AZ(pthread_mutex_unlock(&expmtx));
			if (o != NULL)
				printf("Root: %p %d (%d)\n",
				    (void*)o, o->ttl, o->ttl - t);
			sleep(1);
			continue;
		}
		printf("Root: %p %d (%d)\n", (void*)o, o->ttl, o->ttl - t);
		binheap_delete(exp_heap, 0);
		AZ(pthread_mutex_unlock(&expmtx));
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

/*--------------------------------------------------------------------*/

void
EXP_Init(void)
{

	AZ(pthread_create(&exp_thread, NULL, exp_main, NULL));
	AZ(pthread_mutex_init(&expmtx, NULL));
	exp_heap = binheap_new(NULL, object_cmp, NULL);
	assert(exp_heap != NULL);
}
