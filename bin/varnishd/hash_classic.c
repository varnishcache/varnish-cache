/*
 * $Id$
 *
 * A classic bucketed hash
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <md5.h>

#include <libvarnish.h>
#include <cache.h>

/*--------------------------------------------------------------------*/

struct hcl_entry {
	unsigned		magic;
#define HCL_ENTRY_MAGIC		0x0ba707bf
	TAILQ_ENTRY(hcl_entry)	list;
	char			*key1;
	char			*key2;
	struct objhead		*oh;
	unsigned		refcnt;
	unsigned		hash;
	unsigned		mtx;
};

TAILQ_HEAD(hcl_head, hcl_entry);

static struct hcl_head *hcl_head;
static unsigned hcl_nhash = 256;
static unsigned hcl_nmtx = 16;
static pthread_mutex_t *hcl_mutex;

/*--------------------------------------------------------------------
 * The ->init method allows the management process to pass arguments
 */

static int
hcl_init(const char *p)
{
	int i;
	unsigned u1, u2;

	i = sscanf(p, "%u,%u", &u1, &u2);
	if (i <= 0)
		return (0);
	if (u1 == 0 || (i == 2 && (u2 == 0 || u2 > u1))) {
		fprintf(stderr, "Invallid parameters to hash \"classic\":\n");
		fprintf(stderr,
		    "\t-h classic,<bucket count>[,<buckets per mutex>]\n");
		return (1);
	}
	hcl_nhash = u1;
	if (i == 1) {
		hcl_nmtx = hcl_nhash / 16;
		if (hcl_nmtx <  1)
			hcl_nmtx = 1;
		return(0);
	} else {
		hcl_nmtx = hcl_nhash / u2;
		if (hcl_nmtx < 1)
			hcl_nmtx = 1;
	}
	fprintf(stderr, "Classic hash: %u buckets %u mutexes\n",
	    hcl_nhash, hcl_nmtx);
	return (0);
}

/*--------------------------------------------------------------------
 * The ->start method is called during cache process start and allows 
 * initialization to happen before the first lookup.
 */

static void
hcl_start(void)
{
	unsigned u;

	hcl_head = calloc(sizeof *hcl_head, hcl_nhash);
	assert(hcl_head != NULL);
	hcl_mutex = calloc(sizeof *hcl_mutex, hcl_nmtx);
	assert(hcl_mutex != NULL);


	for (u = 0; u < hcl_nhash; u++)
		TAILQ_INIT(&hcl_head[u]);

	for (u = 0; u < hcl_nmtx; u++)
		AZ(pthread_mutex_init(&hcl_mutex[u], NULL));
}

/*--------------------------------------------------------------------
 * Lookup and possibly insert element.
 * If nobj != NULL and the lookup does not find key, nobj is inserted.
 * If nobj == NULL and the lookup does not find key, NULL is returned.
 * A reference to the returned object is held.
 */

static struct objhead *
hcl_lookup(const char *key1, const char *key2, struct objhead *noh)
{
	struct hcl_entry *he, *he2;
	MD5_CTX c;
	unsigned char md5[MD5_DIGEST_LENGTH];
	unsigned u1, u2;
	int i;

	CHECK_OBJ_NOTNULL(noh, OBJHEAD_MAGIC);
	MD5Init(&c);
	MD5Update(&c, key1, strlen(key1));
	MD5Update(&c, "", 1);
	MD5Update(&c, key2, strlen(key2));
	MD5Final(md5, &c);
	memcpy(&u1, md5, sizeof u1);
	u1 %= hcl_nhash;
	memcpy(&u2, md5 + sizeof u1, sizeof u2);
	u2 %= hcl_nmtx;

	AZ(pthread_mutex_lock(&hcl_mutex[u2]));
	TAILQ_FOREACH(he, &hcl_head[u1], list) {
		CHECK_OBJ_NOTNULL(he, HCL_ENTRY_MAGIC);
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
		noh = he->oh;
		noh->hashpriv = he;
		AZ(pthread_mutex_unlock(&hcl_mutex[u2]));
		return (noh);
	}
	if (noh == NULL) {
		AZ(pthread_mutex_unlock(&hcl_mutex[u2]));
		return (NULL);
	}
	he2 = calloc(sizeof *he2, 1);
	assert(he2 != NULL);
	he2->magic = HCL_ENTRY_MAGIC;
	he2->oh = noh;
	he2->refcnt = 1;
	he2->hash = u1;
	he2->mtx = u2;
	he2->key1 = strdup(key1);
	assert(he2->key1 != NULL);
	he2->key2 = strdup(key2);
	assert(he2->key2 != NULL);
	noh->hashpriv = he2;
	if (he != NULL)
		TAILQ_INSERT_BEFORE(he, he2, list);
	else
		TAILQ_INSERT_TAIL(&hcl_head[u1], he2, list);
	AZ(pthread_mutex_unlock(&hcl_mutex[u2]));
	return (noh);
}

/*--------------------------------------------------------------------
 * Dereference and if no references are left, free.
 */

static int
hcl_deref(struct objhead *oh)
{
	struct hcl_entry *he;
	int ret;
	unsigned mtx;

	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	CAST_OBJ_NOTNULL(he, oh->hashpriv, HCL_ENTRY_MAGIC);
	mtx = he->mtx;
	AZ(pthread_mutex_lock(&hcl_mutex[mtx]));
	if (--he->refcnt >= 0) {
		AZ(pthread_mutex_unlock(&hcl_mutex[mtx]));
		return (1)
	}
	TAILQ_REMOVE(&hcl_head[he->hash], he, list);
	AZ(pthread_mutex_unlock(&hcl_mutex[mtx]));
	free(he->key1);
	free(he->key2);
	free(he);
	return (0);
}

/*--------------------------------------------------------------------*/

struct hash_slinger hcl_slinger = {
	"classic",
	hcl_init,
	hcl_start,
	hcl_lookup,
	hcl_deref,
};
