/*
 * $Id$
 *
 * XXX: automatic thread-pool size adaptation.
 */

#include <sys/types.h>
#include <sys/uio.h>

#ifdef HAVE_SENDFILE
#if defined(__FreeBSD__)
#include <sys/socket.h>
#elif defined(__linux__)
#include <sys/sendfile.h>
#else
#error Unknown sendfile() implementation
#endif
#endif /* HAVE_SENDFILE */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "heritage.h"
#include "shmlog.h"
#include "vcl.h"
#include "cli_priv.h"
#include "cache.h"

TAILQ_HEAD(workerhead, worker);

/* Number of work requests queued in excess of worker threads available */

struct wq {
	MTX 			mtx;
	struct workerhead	idle;
	TAILQ_HEAD(, workreq)   req;
	unsigned		overflow;
};

static MTX			tmtx;

static struct wq		**wq;
static unsigned			nwq;

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

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	assert(fd >= 0);
	assert(len > 0);

#if defined(__FreeBSD__)
	do {
		struct sf_hdtr sfh;
		memset(&sfh, 0, sizeof sfh);
		if (w->niov > 0) {
			sfh.headers = w->iov;
			sfh.hdr_cnt = w->niov;
		}
		if (sendfile(fd, *w->wfd, off, len, &sfh, NULL, 0) != 0)
			w->werr++;
		w->liov = 0;
		w->niov = 0;
	} while (0);
#elif defined(__linux__)
	do {
		if (WRK_Flush(w) == 0) {
			if (sendfile(*w->wfd, fd, &off, len) != 0)
				w->werr++;
		}
	} while (0);
#else
#error Unknown sendfile() implementation
#endif
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
	struct wq *qp;
	char c;

	qp = priv;
	w = &ww;
	memset(w, 0, sizeof *w);
	w->magic = WORKER_MAGIC;
	w->idle = time(NULL);
	AZ(pipe(w->pipe));

	VSL(SLT_WorkThread, 0, "%p start", w);
	LOCK(&qp->mtx);
	VSL_stats->n_wrk_create++;
	VSL_stats->n_wrk_busy++;
	while (1) {
		CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);

		/* Process overflow requests, if any */
		if (qp->overflow > 0) {
			qp->overflow--;
			w->wrq = TAILQ_FIRST(&qp->req);
			AN(w->wrq);
			TAILQ_REMOVE(&qp->req, w->wrq, list);
			UNLOCK(&qp->mtx);
			wrk_do_one(w);
			LOCK(&qp->mtx);
			continue;
		}
		
		TAILQ_INSERT_HEAD(&qp->idle, w, list);
		assert(w->idle != 0);
		VSL_stats->n_wrk_busy--;
		UNLOCK(&qp->mtx);
		assert(1 == read(w->pipe[0], &c, 1));
		if (w->idle == 0)
			break;
		wrk_do_one(w);
		LOCK(&qp->mtx);
	}
	LOCK(&tmtx);
	VSL_stats->n_wrk--;
	UNLOCK(&tmtx);
	VSL(SLT_WorkThread, 0, "%p end", w);
	if (w->vcl != NULL)
		VCL_Rel(&w->vcl);
	close(w->pipe[0]);
	close(w->pipe[1]);
	if (w->srcaddr != NULL)
		free(w->srcaddr);
	if (w->nobjhead != NULL)
		free(w->nobjhead);
	if (w->nobj!= NULL)
		free(w->nobj);
	return (NULL);
}

/*--------------------------------------------------------------------*/

void
WRK_QueueSession(struct sess *sp)
{
	struct worker *w;
	pthread_t tp;
	struct wq *qp;
	static unsigned nq;
	unsigned onq;

	onq = nq + 1;
	if (onq > nwq)
		onq = 0;
	sp->workreq.sess = sp;
	qp = wq[onq];
	nq = onq;

	LOCK(&qp->mtx);

	/* If there are idle threads, we tickle the first one into action */
	w = TAILQ_FIRST(&qp->idle);
	if (w != NULL) {
		TAILQ_REMOVE(&qp->idle, w, list);
		VSL_stats->n_wrk_busy++;
		UNLOCK(&qp->mtx);
		w->wrq = &sp->workreq;
		assert(1 == write(w->pipe[1], w, 1));
		return;
	}
	
	TAILQ_INSERT_TAIL(&qp->req, &sp->workreq, list);
	qp->overflow++;
	UNLOCK(&qp->mtx);

	LOCK(&tmtx);
	VSL_stats->n_wrk_queue++;
	/* Can we create more threads ? */
	if (VSL_stats->n_wrk >= params->wthread_max) {
		VSL_stats->n_wrk_max++;
		UNLOCK(&tmtx);
		return;
	}

	/* Try to create a thread */
	VSL_stats->n_wrk++;
	UNLOCK(&tmtx);

	if (!pthread_create(&tp, NULL, wrk_thread, qp)) {
		AZ(pthread_detach(tp));
		return;
	}

	VSL(SLT_Debug, 0, "Create worker thread failed %d %s",
	    errno, strerror(errno));

	LOCK(&tmtx);
	/* Register overflow */
	VSL_stats->n_wrk--;
	VSL_stats->n_wrk_failed++;
	UNLOCK(&tmtx);
}

/*--------------------------------------------------------------------*/

static void
wrk_addpools(unsigned t)
{
	struct wq **pwq, **owq;
	unsigned u;

	if (t <= nwq)
		return;

	pwq = calloc(sizeof *pwq, params->wthread_pools);
	if (pwq == NULL)
		return;
	if (wq != NULL)
		memcpy(pwq, wq, sizeof *pwq * nwq);
	owq = wq;
	wq = pwq;
	for (u = nwq; u < t; u++) {
		wq[u] = calloc(sizeof *wq[u], 1);
		XXXAN(wq[u]);
		MTX_INIT(&wq[u]->mtx);
		TAILQ_INIT(&wq[u]->idle);
		TAILQ_INIT(&wq[u]->req);
	}
	free(owq);
	nwq = t;
}

/*--------------------------------------------------------------------*/
	
static void *
wrk_reaperthread(void *priv)
{
	time_t	now;
	struct worker *w;
	struct wq *qp;
	unsigned u;

	(void)priv;
	while (1) {
		wrk_addpools(params->wthread_pools);
		sleep(1);
		if (VSL_stats->n_wrk <= params->wthread_min)
			continue; 
		now = time(NULL);
		for (u = 0; u < nwq; u++) {
			qp = wq[u];
			LOCK(&qp->mtx);
			w = TAILQ_LAST(&qp->idle, workerhead);
			if (w != NULL &&
			   (w->idle + params->wthread_timeout < now ||
			    VSL_stats->n_wrk > params->wthread_max))
				TAILQ_REMOVE(&qp->idle, w, list);
			else 
				w = NULL;
			UNLOCK(&qp->mtx);
			if (w == NULL)
				continue;
			w->idle = 0;
			assert(1 == write(w->pipe[1], w, 1));
		}
	}
	INCOMPL();
}

/*--------------------------------------------------------------------*/

void
WRK_Init(void)
{
	pthread_t tp;

	wrk_addpools(params->wthread_pools);
	MTX_INIT(&tmtx);
	AZ(pthread_create(&tp, NULL, wrk_reaperthread, NULL));
	AZ(pthread_detach(tp));
}

/*--------------------------------------------------------------------*/

void
cli_func_dump_pool(struct cli *cli, char **av, void *priv)
{

	(void)cli;
	(void)av;
	(void)priv;
}
