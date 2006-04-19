/*
 * $Id$
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/queue.h>

#include <libvarnish.h>
#include <vcl_lang.h>
#include <cache.h>

struct hsl_entry {
	TAILQ_ENTRY(hsl_entry)	list;
	struct object		*obj;
};

static TAILQ_HEAD(, hsl_entry)	hsl_head = TAILQ_HEAD_INITIALIZER(hsl_head);
static pthread_mutex_t hsl_mutex;

static void
hsl_init(void)
{

	AZ(pthread_mutex_init(&hsl_mutex, NULL));
}

static struct object *
hsl_lookup(unsigned char *key, struct object *nobj)
{
	struct hsl_entry *he, *he2;
	int i;

	AZ(pthread_mutex_lock(&hsl_mutex));
	TAILQ_FOREACH(he, &hsl_head, list) {
		i = memcmp(key, he->obj->hash, sizeof he->obj->hash);
		if (i == 0) {
			he->obj->refcnt++;
			nobj = he->obj;
			AZ(pthread_mutex_unlock(&hsl_mutex));
			return (nobj);
		}
		if (i < 0)
			continue;
		if (nobj == NULL) {
			AZ(pthread_mutex_unlock(&hsl_mutex));
			return (NULL);
		}
		break;
	}
	he2 = calloc(sizeof *he2, 1);
	assert(he2 != NULL);
	he2->obj = nobj;
	nobj->refcnt++;
	memcpy(nobj->hash, key, sizeof nobj->hash);
	if (he != NULL)
		TAILQ_INSERT_BEFORE(he, he2, list);
	else
		TAILQ_INSERT_TAIL(&hsl_head, he2, list);
	AZ(pthread_mutex_unlock(&hsl_mutex));
	return (nobj);
}

static void
hsl_deref(struct object *obj)
{

	AZ(pthread_mutex_lock(&hsl_mutex));
	obj->refcnt--;
	AZ(pthread_mutex_unlock(&hsl_mutex));
}

static void
hsl_purge(struct object *obj)
{
	struct hsl_entry *he;

	assert(obj->refcnt > 0);
	AZ(pthread_mutex_lock(&hsl_mutex));
	TAILQ_FOREACH(he, &hsl_head, list) {
		if (he->obj == obj) {
			TAILQ_REMOVE(&hsl_head, he, list);
			AZ(pthread_mutex_unlock(&hsl_mutex));
			free(he);
			return;
		}
	}
	assert(he != NULL);
}

struct hash_slinger hsl_slinger = {
	"simple_list",
	hsl_init,
	hsl_lookup,
	hsl_deref,
	hsl_purge
};
