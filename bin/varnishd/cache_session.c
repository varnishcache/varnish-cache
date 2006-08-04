/*
 * $Id$
 *
 * Session and Client management.
 *
 * The srcaddr structures are kept around only as a convenience feature to
 * make it possible to track down offenders and misconfigured caches.
 * As such it is pure overhead and we do not want to spend too much time
 * on maintaining it.
 *
 * We identify srcaddrs instead of full addr+port because the desired level
 * of granularity is "whois is abuse@ or tech-c@ in the RIPE database.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include "libvarnish.h"
#include "heritage.h"
#include "shmlog.h"
#include "cache.h"

#define CLIENT_HASH			256
#define CLIENT_TTL			30

/*--------------------------------------------------------------------*/

struct sessmem {
	unsigned		magic;
#define SESSMEM_MAGIC		0x555859c5

	struct sess		sess;
	struct http		http;
	struct sockaddr		sockaddr[2];	/* INET6 hack */
};

/*--------------------------------------------------------------------*/

TAILQ_HEAD(srcaddrhead ,srcaddr);

static struct srcaddrhead	srcaddr_hash[CLIENT_HASH];
static pthread_mutex_t		ses_mtx;

/*--------------------------------------------------------------------
 * Assign a srcaddr to this session.
 *
 * We use a simple hash over the ascii representation of the address
 * because it is nice and modular.  I'm not sure how much improvement
 * using the binary address would be anyway.
 *
 * Each hash bucket is sorted in least recently used order and if we
 * need to make a new entry we recycle the first expired entry we find.
 * If we find more expired entries during our search, we delete them.
 */

void
SES_RefSrcAddr(struct sess *sp)
{
	unsigned u, v;
	char *p;
	struct srcaddr *c, *c2, *c3;
	struct srcaddrhead *ch;
	time_t now;

	assert(sp->srcaddr == NULL);
	for (u = 0, p = sp->addr; *p; p++)
		u += u + *p;
	v = u % CLIENT_HASH;
	ch = &srcaddr_hash[v];
	now = time(NULL);

	AZ(pthread_mutex_lock(&ses_mtx));
	c3 = NULL;
	TAILQ_FOREACH_SAFE(c, ch, list, c2) {
		if (c->hash == u && !strcmp(c->addr, sp->addr)) {
			if (c->nref == 0)
				VSL_stats->n_srcaddr_act++;
			c->nref++;
			c->ttl = now + CLIENT_TTL;
			sp->srcaddr = c;
			TAILQ_REMOVE(ch, c, list);
			TAILQ_INSERT_TAIL(ch, c, list);
			if (0 && c3 != NULL) {
				TAILQ_REMOVE(ch, c3, list);
				VSL_stats->n_srcaddr--;
				free(c3);
			}
			AZ(pthread_mutex_unlock(&ses_mtx));
			return;
		}
		if (c->nref > 0 || c->ttl > now)
			continue;
		if (c3 == NULL) {
			c3 = c;
			continue;
		}
		TAILQ_REMOVE(ch, c, list);
		free(c);
		VSL_stats->n_srcaddr--;
	}
	if (c3 == NULL) {
		c3 = malloc(sizeof *c3);
		assert(c3 != NULL);
		if (c3 != NULL)
			VSL_stats->n_srcaddr++;
	} else
		TAILQ_REMOVE(ch, c3, list);
	assert (c3 != NULL);
	if (c3 != NULL) {
		memset(c3, 0, sizeof *c3);
		strcpy(c3->addr, sp->addr);
		c3->hash = u;
		c3->acct.first = now;
		c3->ttl = now + CLIENT_TTL;
		c3->nref = 1;
		c3->sah = ch;
		VSL_stats->n_srcaddr_act++;
		TAILQ_INSERT_TAIL(ch, c3, list);
		sp->srcaddr = c3;
	}
	AZ(pthread_mutex_unlock(&ses_mtx));
}

static void
ses_sum_acct(struct acct *sum, struct acct *inc)
{

	sum->sess += inc->sess;
	sum->req += inc->req;
	sum->pipe += inc->pipe;
	sum->pass += inc->pass;
	sum->fetch += inc->fetch;
	sum->hdrbytes += inc->hdrbytes;
	sum->bodybytes += inc->bodybytes;
}

void
SES_Charge(struct sess *sp)
{
	struct acct *a = &sp->wrk->acct;
	struct acct *b = &sp->srcaddr->acct;

	ses_sum_acct(&sp->acct, a);
	
	AZ(pthread_mutex_lock(&ses_mtx));
	ses_sum_acct(b, a);
	VSL(SLT_StatAddr, 0, "%s 0 %d %ju %ju %ju %ju %ju %ju %ju",
	    sp->srcaddr->addr, time(NULL) - b->first,
	    b->sess, b->req, b->pipe, b->pass,
	    b->fetch, b->hdrbytes, b->bodybytes);
	VSL_stats->s_sess += a->sess;
	VSL_stats->s_req += a->req;
	VSL_stats->s_pipe += a->pipe;
	VSL_stats->s_pass += a->pass;
	VSL_stats->s_fetch += a->fetch;
	VSL_stats->s_hdrbytes += a->hdrbytes;
	VSL_stats->s_bodybytes += a->bodybytes;
	AZ(pthread_mutex_unlock(&ses_mtx));
	memset(a, 0, sizeof *a);
}

static void
ses_relsrcaddr(struct sess *sp)
{

	if (sp->srcaddr == NULL) {
		/* If we never get to work pool (illegal req) */
		return;
	}
	assert(sp->srcaddr != NULL);
	AZ(pthread_mutex_lock(&ses_mtx));
	assert(sp->srcaddr->nref > 0);
	sp->srcaddr->nref--;
	if (sp->srcaddr->nref == 0)
		VSL_stats->n_srcaddr_act--;
	sp->srcaddr = NULL;
	AZ(pthread_mutex_unlock(&ses_mtx));
}

/*--------------------------------------------------------------------*/

struct sess *
SES_New(struct sockaddr *addr, unsigned len)
{
	struct sessmem *sm;

	sm = calloc(
	    sizeof *sm + heritage.mem_workspace,
	    1);
	if (sm == NULL)
		return (NULL);
	sm->magic = SESSMEM_MAGIC;
	VSL_stats->n_sess++;
	sm->sess.magic = SESS_MAGIC;
	sm->sess.mem = sm;
	sm->sess.http = &sm->http;

	sm->sess.sockaddr = sm->sockaddr;
	assert(len  < sizeof(sm->sockaddr));
	if (addr != NULL) {
		memcpy(sm->sess.sockaddr, addr, len);
		sm->sess.sockaddrlen = len;
	}

	http_Setup(&sm->http, (void *)(sm + 1), heritage.mem_workspace);

	sm->sess.acct.first = time(NULL);

	return (&sm->sess);
}

void
SES_Delete(struct sess *sp)
{
	struct acct *b = &sp->acct;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(sp->obj == NULL);
	assert(sp->vcl == NULL);
	VSL_stats->n_sess--;
	ses_relsrcaddr(sp);
	VSL(SLT_StatSess, sp->id, "%s %s %d %ju %ju %ju %ju %ju %ju %ju",
	    sp->addr, sp->port, time(NULL) - b->first,
	    b->sess, b->req, b->pipe, b->pass,
	    b->fetch, b->hdrbytes, b->bodybytes);
	CHECK_OBJ_NOTNULL(sp->mem, SESSMEM_MAGIC);
	free(sp->mem);
}

/*--------------------------------------------------------------------*/

void
SES_Init()
{
	int i;

	for (i = 0; i < CLIENT_HASH; i++)
		TAILQ_INIT(&srcaddr_hash[i]);
	AZ(pthread_mutex_init(&ses_mtx, NULL));
}
