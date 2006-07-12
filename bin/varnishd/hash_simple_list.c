/*
 * $Id$
 *
 * This is the reference hash(/lookup) implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libvarnish.h>
#include <cache.h>

/*--------------------------------------------------------------------*/

struct hsl_entry {
	TAILQ_ENTRY(hsl_entry)	list;
	char			*key1;
	char			*key2;
	struct objhead		*obj;
	unsigned		refcnt;
};

static TAILQ_HEAD(, hsl_entry)	hsl_head = TAILQ_HEAD_INITIALIZER(hsl_head);
static pthread_mutex_t hsl_mutex;

/*--------------------------------------------------------------------
 * The ->init method is called during process start and allows 
 * initialization to happen before the first lookup.
 */

static void
hsl_start(void)
{

	AZ(pthread_mutex_init(&hsl_mutex, NULL));
}

/*--------------------------------------------------------------------
 * Lookup and possibly insert element.
 * If nobj != NULL and the lookup does not find key, nobj is inserted.
 * If nobj == NULL and the lookup does not find key, NULL is returned.
 * A reference to the returned object is held.
 */

static struct objhead *
hsl_lookup(const char *key1, const char *key2, struct objhead *nobj)
{
	struct hsl_entry *he, *he2;
	int i;

	AZ(pthread_mutex_lock(&hsl_mutex));
	TAILQ_FOREACH(he, &hsl_head, list) {
		i = strcmp(key1, he->key1);
		if (i < 0)
			continue;
		if (i > 0) 
			break;
		i = strcmp(key2, he->key2);
		if (i < 0)
			continue;
		if (i > 0) 
			break;
		he->refcnt++;
		nobj = he->obj;
		nobj->hashpriv = he;
		AZ(pthread_mutex_unlock(&hsl_mutex));
		return (nobj);
	}
	if (nobj == NULL) {
		AZ(pthread_mutex_unlock(&hsl_mutex));
		return (NULL);
	}
	he2 = calloc(sizeof *he2, 1);
	assert(he2 != NULL);
	he2->obj = nobj;
	he2->refcnt = 1;
	he2->key1 = strdup(key1);
	assert(he2->key1 != NULL);
	he2->key2 = strdup(key2);
	assert(he2->key2 != NULL);
	nobj->hashpriv = he2;
	if (he != NULL)
		TAILQ_INSERT_BEFORE(he, he2, list);
	else
		TAILQ_INSERT_TAIL(&hsl_head, he2, list);
	AZ(pthread_mutex_unlock(&hsl_mutex));
	return (nobj);
}

/*--------------------------------------------------------------------
 * Dereference and if no references are left, free.
 */

static int
hsl_deref(struct objhead *obj)
{
	struct hsl_entry *he;
	int ret;

	assert(obj->hashpriv != NULL);
	he = obj->hashpriv;
	AZ(pthread_mutex_lock(&hsl_mutex));
	if (--he->refcnt == 0) {
		free(he->key1);
		free(he->key2);
		TAILQ_REMOVE(&hsl_head, he, list);
		free(he);
		ret = 0;
	} else
		ret = 1;
	AZ(pthread_mutex_unlock(&hsl_mutex));
	return (ret);
}

/*--------------------------------------------------------------------*/

struct hash_slinger hsl_slinger = {
	"simple_list",
	NULL,
	hsl_start,
	hsl_lookup,
	hsl_deref,
};
