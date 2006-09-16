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
 *
 * XXX: The two-list session management is actually not a good idea
 * XXX: come to think of it, because we want the sessions reused in
 * XXX: Most Recently Used order.
 * XXX: Another and maybe more interesting option would be to cache 
 * XXX: free sessions in the worker threads and postpone session 
 * XXX: allocation until then.  This does not quite implment MRU order
 * XXX: but it does save some locking, although not that much because
 * XXX: we still have to do the source-addr lookup.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include "heritage.h"
#include "shmlog.h"
#include "cache.h"

/*--------------------------------------------------------------------*/

struct sessmem {
	unsigned		magic;
#define SESSMEM_MAGIC		0x555859c5

	struct sess		sess;
	struct http		http;
	struct sockaddr		sockaddr[2];	/* INET6 hack */
	unsigned		workspace;
	TAILQ_ENTRY(sessmem)	list;
};

static TAILQ_HEAD(,sessmem)	ses_free_mem[2] = {
    TAILQ_HEAD_INITIALIZER(ses_free_mem[0]),
    TAILQ_HEAD_INITIALIZER(ses_free_mem[1]),
};

static unsigned ses_qp;
static MTX			ses_mem_mtx;

/*--------------------------------------------------------------------*/

struct srcaddr {
	unsigned		magic;
#define SRCADDR_MAGIC		0x375111db

	unsigned		hash;
	TAILQ_ENTRY(srcaddr)	list;
	struct srcaddrhead	*sah;

	char			addr[TCP_ADDRBUFSIZE];
	unsigned		nref;

	time_t			ttl;

	struct acct		acct;
};

static struct srcaddrhead {
	unsigned		magic;
#define SRCADDRHEAD_MAGIC	0x38231a8b
	TAILQ_HEAD(,srcaddr)	head;
	MTX			mtx;
} *srchash;
	
unsigned			nsrchash;
static MTX			stat_mtx;

/*--------------------------------------------------------------------
 * Assign a srcaddr to this session.
 *
 * Each hash bucket is sorted in least recently used order and if we
 * need to make a new entry we recycle the first expired entry we find.
 * If we find more expired entries during our search, we delete them.
 */

void
SES_RefSrcAddr(struct sess *sp)
{
	unsigned u, v;
	struct srcaddr *c, *c2, *c3;
	struct srcaddrhead *ch;
	time_t now;

	if (params->srcaddr_ttl == 0) {
		sp->srcaddr = NULL;
		return;
	}
	AZ(sp->srcaddr);
	u = crc32_2s(sp->addr, "");
	v = u % nsrchash;
	ch = &srchash[v];
	CHECK_OBJ(ch, SRCADDRHEAD_MAGIC);
	now = sp->t_open.tv_sec;
	if (sp->wrk->srcaddr == NULL) {
		sp->wrk->srcaddr = calloc(sizeof *sp->wrk->srcaddr, 1);
		XXXAN(sp->wrk->srcaddr);
	}

	LOCK(&ch->mtx);
	c3 = NULL;
	TAILQ_FOREACH_SAFE(c, &ch->head, list, c2) {
		if (c->hash == u && !strcmp(c->addr, sp->addr)) {
			if (c->nref == 0)
				VSL_stats->n_srcaddr_act++;
			c->nref++;
			c->ttl = now + params->srcaddr_ttl;
			sp->srcaddr = c;
			TAILQ_REMOVE(&ch->head, c, list);
			TAILQ_INSERT_TAIL(&ch->head, c, list);
			if (c3 != NULL) {
				TAILQ_REMOVE(&ch->head, c3, list);
				VSL_stats->n_srcaddr--;
			}
			UNLOCK(&ch->mtx);
			if (c3 != NULL)
				free(c3);
			return;
		}
		if (c->nref > 0 || c->ttl > now)
			continue;
		if (c3 == NULL)
			c3 = c;
	}
	if (c3 == NULL) {
		c3 = sp->wrk->srcaddr;
		sp->wrk->srcaddr = NULL;
		VSL_stats->n_srcaddr++;
	} else
		TAILQ_REMOVE(&ch->head, c3, list);
	AN(c3);
	memset(c3, 0, sizeof *c3);
	c3->magic = SRCADDR_MAGIC;
	strcpy(c3->addr, sp->addr);
	c3->hash = u;
	c3->acct.first = now;
	c3->ttl = now + params->srcaddr_ttl;
	c3->nref = 1;
	c3->sah = ch;
	VSL_stats->n_srcaddr_act++;
	TAILQ_INSERT_TAIL(&ch->head, c3, list);
	sp->srcaddr = c3;
	UNLOCK(&ch->mtx);
}

/*--------------------------------------------------------------------*/

static void
ses_relsrcaddr(struct sess *sp)
{
	struct srcaddrhead *ch;

	if (sp->srcaddr == NULL)
		return;
	CHECK_OBJ(sp->srcaddr, SRCADDR_MAGIC);
	ch = sp->srcaddr->sah;
	CHECK_OBJ(ch, SRCADDRHEAD_MAGIC);
	LOCK(&ch->mtx);
	assert(sp->srcaddr->nref > 0);
	sp->srcaddr->nref--;
	if (sp->srcaddr->nref == 0)
		VSL_stats->n_srcaddr_act--;
	sp->srcaddr = NULL;
	UNLOCK(&ch->mtx);
}

/*--------------------------------------------------------------------*/

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
	struct acct *a = &sp->acct;
	struct acct *b;

	if (sp->srcaddr != NULL) {
		CHECK_OBJ(sp->srcaddr, SRCADDR_MAGIC);
		LOCK(&sp->srcaddr->sah->mtx);
		b = &sp->srcaddr->acct;
		ses_sum_acct(b, a);
		VSL(SLT_StatAddr, 0, "%s 0 %d %ju %ju %ju %ju %ju %ju %ju",
		    sp->srcaddr->addr, sp->t_end.tv_sec - b->first,
		    b->sess, b->req, b->pipe, b->pass,
		    b->fetch, b->hdrbytes, b->bodybytes);
		UNLOCK(&sp->srcaddr->sah->mtx);
	}
	LOCK(&stat_mtx);
	VSL_stats->s_sess += a->sess;
	VSL_stats->s_req += a->req;
	VSL_stats->s_pipe += a->pipe;
	VSL_stats->s_pass += a->pass;
	VSL_stats->s_fetch += a->fetch;
	VSL_stats->s_hdrbytes += a->hdrbytes;
	VSL_stats->s_bodybytes += a->bodybytes;
	UNLOCK(&stat_mtx);
	memset(a, 0, sizeof *a);
}

/*--------------------------------------------------------------------*/

struct sess *
SES_New(struct sockaddr *addr, unsigned len)
{
	struct sessmem *sm;
	unsigned u;


	/*
	 * One of the two queues is unlocked because only one
	 * thread ever gets here to empty it.
	 */
	assert(ses_qp <= 1);
	sm = TAILQ_FIRST(&ses_free_mem[ses_qp]);
	if (sm == NULL) {
		/*
		 * If that queue is empty, flip queues holding the lock
		 * and try the new unlocked queue.
		 */
		LOCK(&ses_mem_mtx);
		ses_qp = 1 - ses_qp;
		UNLOCK(&ses_mem_mtx);
		sm = TAILQ_FIRST(&ses_free_mem[ses_qp]);
	}
	if (sm != NULL) {
		TAILQ_REMOVE(&ses_free_mem[ses_qp], sm, list);
	} else {
		/*
		 * If that fails, alloc new one.
		 */
		u = params->mem_workspace;
		sm = malloc(sizeof *sm + u);
		if (sm == NULL)
			return (NULL);
		sm->magic = SESSMEM_MAGIC;
		sm->workspace = u;
		VSL_stats->n_sess_mem++;
	}
	if (sm == NULL)
		return (NULL);
	CHECK_OBJ_NOTNULL(sm, SESSMEM_MAGIC);
	VSL_stats->n_sess++;
	memset(&sm->sess, 0, sizeof sm->sess);
	sm->sess.magic = SESS_MAGIC;
	sm->sess.mem = sm;
	sm->sess.http = &sm->http;

	sm->sess.sockaddr = sm->sockaddr;
	assert(len < sizeof(sm->sockaddr));
	if (addr != NULL) {
		memcpy(sm->sess.sockaddr, addr, len);
		sm->sess.sockaddrlen = len;
	}

	http_Setup(&sm->http, (void *)(sm + 1), sm->workspace);

	return (&sm->sess);
}

void
SES_Delete(struct sess *sp)
{
	struct acct *b = &sp->acct;
	struct sessmem *sm;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	sm = sp->mem;
	CHECK_OBJ_NOTNULL(sm, SESSMEM_MAGIC);
	
	AZ(sp->obj);
	AZ(sp->vcl);
	VSL_stats->n_sess--;
	ses_relsrcaddr(sp);
	VSL(SLT_StatSess, sp->id, "%s %s %d %ju %ju %ju %ju %ju %ju %ju",
	    sp->addr, sp->port, sp->t_end.tv_sec - b->first,
	    b->sess, b->req, b->pipe, b->pass,
	    b->fetch, b->hdrbytes, b->bodybytes);
	if (sm->workspace != params->mem_workspace) { 
		VSL_stats->n_sess_mem--;
		free(sm);
	} else {
		LOCK(&ses_mem_mtx);
		TAILQ_INSERT_HEAD(&ses_free_mem[1 - ses_qp], sm, list);
		UNLOCK(&ses_mem_mtx);
	}
}

/*--------------------------------------------------------------------*/

void
SES_Init()
{
	int i;

	nsrchash = params->srcaddr_hash;
	srchash = calloc(sizeof *srchash, nsrchash);
	XXXAN(srchash);
	for (i = 0; i < nsrchash; i++) {
		srchash[i].magic = SRCADDRHEAD_MAGIC;
		TAILQ_INIT(&srchash[i].head);
		MTX_INIT(&srchash[i].mtx);
	}
	MTX_INIT(&stat_mtx);
	MTX_INIT(&ses_mem_mtx);
}
