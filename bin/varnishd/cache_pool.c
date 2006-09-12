/*
 * $Id$
 *
 * XXX: automatic thread-pool size adaptation.
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SENDFILE
#include <sys/uio.h>
#include <sys/socket.h>
#endif /* HAVE_SENDFILE */
#include <unistd.h>

#include "heritage.h"
#include "shmlog.h"
#include "vcl.h"
#include "cli_priv.h"
#include "cache.h"

static pthread_mutex_t wrk_mtx;

/* Number of work requests queued in excess of worker threads available */
static unsigned		wrk_overflow;

TAILQ_HEAD(workerhead, worker);

static struct workerhead wrk_idle = TAILQ_HEAD_INITIALIZER(wrk_idle);
static struct workerhead wrk_busy = TAILQ_HEAD_INITIALIZER(wrk_busy);
static TAILQ_HEAD(, workreq) wrk_reqhead = TAILQ_HEAD_INITIALIZER(wrk_reqhead);

/*--------------------------------------------------------------------
 * Write data to fd
 * We try to use writev() if possible in order to minimize number of
 * syscalls made and packets sent.  It also just might allow the worker
 * thread to complete the request without holding stuff locked.
 */

void
WRK_Reset(struct worker *w, int *fd)
{

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	w->werr = 0;
	w->liov = 0;
	w->niov = 0;
	w->wfd = fd;
}

int
WRK_Flush(struct worker *w)
{
	int i;

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	if (*w->wfd < 0 || w->niov == 0 || w->werr)
		return (w->werr);
	i = writev(*w->wfd, w->iov, w->niov);
	if (i != w->liov)
		w->werr++;
	w->liov = 0;
	w->niov = 0;
	return (w->werr);
}

unsigned
WRK_WriteH(struct worker *w, struct http_hdr *hh, const char *suf)
{
	unsigned u;
	
	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	AN(w);
	AN(hh);
	AN(hh->b);
	AN(hh->e);
	u = WRK_Write(w, hh->b, hh->e - hh->b);
	if (suf != NULL)
		u += WRK_Write(w, suf, -1);
	return (u);
}

unsigned
WRK_Write(struct worker *w, const void *ptr, int len)
{

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	if (len == 0 || *w->wfd < 0)
		return (0);
	if (len == -1)
		len = strlen(ptr);
	if (w->niov == MAX_IOVS)
		WRK_Flush(w);
	w->iov[w->niov].iov_base = (void*)(uintptr_t)ptr;
	w->iov[w->niov++].iov_len = len;
	w->liov += len;
	return (len);
}

#ifdef HAVE_SENDFILE
void
WRK_Sendfile(struct worker *w, int fd, off_t off, unsigned len)
{
	struct sf_hdtr sfh;
	int i;

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	assert(fd >= 0);
	assert(len > 0);

	memset(&sfh, 0, sizeof sfh);
	if (w->niov > 0) {
		sfh.headers = w->iov;
		sfh.hdr_cnt = w->niov;
	}
	i = sendfile(fd, *w->wfd, off, len, &sfh, NULL, 0);
	if (i != 0)
		w->werr++;
	w->liov = 0;
	w->niov = 0;
}
#endif /* HAVE_SENDFILE */

/*--------------------------------------------------------------------*/

static void
wrk_do_one(struct worker *w)
{
	struct workreq *wrq;

	AN(w->wrq);
	wrq = w->wrq;
	CHECK_OBJ_NOTNULL(wrq->sess, SESS_MAGIC);
	wrq->sess->wrk = w;
	if (w->nobj != NULL)
		CHECK_OBJ(w->nobj, OBJECT_MAGIC);
	if (w->nobjhead != NULL)
		CHECK_OBJ(w->nobjhead, OBJHEAD_MAGIC);
	CNT_Session(wrq->sess);
	if (w->nobj != NULL)
		CHECK_OBJ(w->nobj, OBJECT_MAGIC);
	if (w->nobjhead != NULL)
		CHECK_OBJ(w->nobjhead, OBJHEAD_MAGIC);
	w->wrq = NULL;
}

static void *
wrk_thread(void *priv)
{
	struct worker *w, ww;
	char c;

	(void)priv;
	w = &ww;
	memset(w, 0, sizeof *w);
	w->magic = WORKER_MAGIC;
	w->idle = time(NULL);
	AZ(pipe(w->pipe));

	VSL(SLT_WorkThread, 0, "%p start", w);
	LOCK(&wrk_mtx);
	VSL_stats->n_wrk_create++;
	TAILQ_INSERT_HEAD(&wrk_busy, w, list);
	VSL_stats->n_wrk_busy++;
	while (1) {
		CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);

		/* Process overflow requests, if any */
		if (wrk_overflow > 0) {
			wrk_overflow--;
			w->wrq = TAILQ_FIRST(&wrk_reqhead);
			AN(w->wrq);
			TAILQ_REMOVE(&wrk_reqhead, w->wrq, list);
			VSL_stats->n_wrk_queue--;
			UNLOCK(&wrk_mtx);
			wrk_do_one(w);
			LOCK(&wrk_mtx);
			continue;
		}
		
		TAILQ_REMOVE(&wrk_busy, w, list);
		TAILQ_INSERT_HEAD(&wrk_idle, w, list);
		assert(w->idle != 0);
		VSL_stats->n_wrk_busy--;
		UNLOCK(&wrk_mtx);
		assert(1 == read(w->pipe[0], &c, 1));
		if (w->idle == 0)
			break;
		wrk_do_one(w);
		LOCK(&wrk_mtx);
	}
	LOCK(&wrk_mtx);
	VSL_stats->n_wrk--;
	UNLOCK(&wrk_mtx);
	VSL(SLT_WorkThread, 0, "%p end", w);
	close(w->pipe[0]);
	close(w->pipe[1]);
	return (NULL);
}

/*--------------------------------------------------------------------*/

void
WRK_QueueSession(struct sess *sp)
{
	struct worker *w;
	pthread_t tp;

	sp->workreq.sess = sp;

	LOCK(&wrk_mtx);

	/* If there are idle threads, we tickle the first one into action */
	w = TAILQ_FIRST(&wrk_idle);
	if (w != NULL) {
		TAILQ_REMOVE(&wrk_idle, w, list);
		TAILQ_INSERT_TAIL(&wrk_busy, w, list);
		VSL_stats->n_wrk_busy++;
		UNLOCK(&wrk_mtx);
		w->wrq = &sp->workreq;
		assert(1 == write(w->pipe[1], w, 1));
		return;
	}
	
	TAILQ_INSERT_TAIL(&wrk_reqhead, &sp->workreq, list);
	VSL_stats->n_wrk_queue++;
	wrk_overflow++;

	/* Can we create more threads ? */
	if (VSL_stats->n_wrk >= params->wthread_max) {
		VSL_stats->n_wrk_max++;
		UNLOCK(&wrk_mtx);
		return;
	}

	/* Try to create a thread */
	VSL_stats->n_wrk++;
	UNLOCK(&wrk_mtx);

	if (!pthread_create(&tp, NULL, wrk_thread, NULL)) {
		AZ(pthread_detach(tp));
		return;
	}

	VSL(SLT_Debug, 0, "Create worker thread failed %d %s",
	    errno, strerror(errno));

	/* Register overflow */
	LOCK(&wrk_mtx);
	VSL_stats->n_wrk--;
	VSL_stats->n_wrk_failed++;
	UNLOCK(&wrk_mtx);
}

/*--------------------------------------------------------------------*/
	
static void *
wrk_reaperthread(void *priv)
{
	time_t	now;
	struct worker *w;

	(void)priv;
	while (1) {
		sleep(1);
		if (VSL_stats->n_wrk <= params->wthread_min)
			continue; 
		now = time(NULL);
		LOCK(&wrk_mtx);
		w = TAILQ_LAST(&wrk_idle, workerhead);
		if (w != NULL &&
		   (w->idle + params->wthread_timeout < now ||
		    VSL_stats->n_wrk <= params->wthread_max))
			TAILQ_REMOVE(&wrk_idle, w, list);
		else 
			w = NULL;
		UNLOCK(&wrk_mtx);
		if (w == NULL)
			continue;
		w->idle = 0;
		assert(1 == write(w->pipe[1], w, 1));
	}
	INCOMPL();
}

/*--------------------------------------------------------------------*/

void
WRK_Init(void)
{
	pthread_t tp;
	int i;

	AZ(pthread_mutex_init(&wrk_mtx, NULL));

	AZ(pthread_create(&tp, NULL, wrk_reaperthread, NULL));
	AZ(pthread_detach(tp));

	VSL(SLT_Debug, 0, "Starting %u worker threads", params->wthread_min);
	for (i = 0; i < params->wthread_min; i++) {
		VSL_stats->n_wrk++;
		AZ(pthread_create(&tp, NULL, wrk_thread, NULL));
		AZ(pthread_detach(tp));
	}
}


/*--------------------------------------------------------------------*/

void
cli_func_dump_pool(struct cli *cli, char **av, void *priv)
{
	unsigned u;
	struct sess *s;
	time_t t;

	(void)av;
	(void)priv;
	struct worker *w;
	LOCK(&wrk_mtx);
	t = time(NULL);
	TAILQ_FOREACH(w, &wrk_busy, list) {
		cli_out(cli, "\n");
		cli_out(cli, "W %p", w);
		if (w->wrq == NULL)
			continue;
		s = w->wrq->sess;
		if (s == NULL)
			continue;
		cli_out(cli, "sess %p fd %d xid %u step %d handling %d age %d",
		    s, s->fd, s->xid, s->step, s->handling,
		    t - s->t_req.tv_sec);
	}
	cli_out(cli, "\n");

	u = 0;
	TAILQ_FOREACH(w, &wrk_idle, list)
		u++;
	cli_out(cli, "%u idle workers\n", u);
	UNLOCK(&wrk_mtx);
}
