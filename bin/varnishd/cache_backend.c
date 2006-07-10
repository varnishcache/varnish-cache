/*
 * $Id$
 *
 * Manage backend connections.
 *
 * For each backend ip number we maintain a shadow backend structure so
 * that backend connections can be reused across VCL changes.
 *
 * For each IP we maintain a list of busy and free backend connections,
 * and free connections are monitored to detect if the backend closes
 * the connection.
 *
 * We recycle backend connections in most recently used order to minimize
 * the number of open connections to any one backend.
 *
 * XXX:
 * I'm not happy about recycling always going through the monitor thread
 * but not doing so is slightly more tricky:  A connection might be reused
 * several times before the monitor thread got around to it, and it would
 * have to double check if it had already armed the event for that connection.
 * Hopefully this is nowhere close to a performance issue, but if it is,
 * it can be optimized at the expense of more complex code.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>
#include <sbuf.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/ioctl.h>

#include "libvarnish.h"
#include "heritage.h"
#include "shmlog.h"
#include "cache.h"

struct vbc_mem {
	struct vbe_conn		vbe;
	struct http		http;
	char			*http_hdr;
};

/* A backend IP */

struct vbe {
	unsigned		ip;
	TAILQ_ENTRY(vbe)	list;
	TAILQ_HEAD(,vbe_conn)	fconn;
	TAILQ_HEAD(,vbe_conn)	bconn;
	unsigned		nconn;
};

static TAILQ_HEAD(,vbe) vbe_head = TAILQ_HEAD_INITIALIZER(vbe_head);

static pthread_mutex_t	vbemtx;

static pthread_t vbe_thread;
static struct event_base *vbe_evb;
static int vbe_pipe[2];

/*--------------------------------------------------------------------*/

static struct vbe_conn *
vbe_new_conn(void)
{
	struct vbc_mem *vbcm;

	vbcm = calloc(
	    sizeof *vbcm +
	    heritage.mem_http_headers * sizeof vbcm->http_hdr +
	    heritage.mem_http_headerspace +
	    heritage.mem_workspace,
	    1);
	if (vbcm == NULL)
		return (NULL);
	VSL_stats->n_vbe_conn++;
	vbcm->vbe.vbcm = vbcm;
	vbcm->vbe.http = &vbcm->http;
	http_Init(&vbcm->http, (void *)(vbcm + 1));
	return (&vbcm->vbe);
}

static void
vbe_delete_conn(struct vbe_conn *vb)
{

	VSL_stats->n_vbe_conn--;
	free(vb->vbcm);
}

/*--------------------------------------------------------------------
 * XXX: we should not call getaddrinfo() every time, we should cache
 * and apply round-robin with blacklisting of entries that do not respond
 * etc.  Periodic re-lookups to capture changed DNS records would also 
 * be a good thing in that case.
 */

static void
connect_to_backend(struct vbe_conn *vc, struct backend *bp)
{
	struct addrinfo *res, *res0, hint;
	int error, s;

	assert(bp != NULL);
	assert(bp->hostname != NULL);
	memset(&hint, 0, sizeof hint);
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	res = NULL;
	error = getaddrinfo(bp->hostname,
	    bp->portname == NULL ? "http" : bp->portname,
	    &hint, &res);
	if (error) {
		if (res != NULL)
			freeaddrinfo(res);
		fprintf(stderr, "getaddrinfo: %s\n", 
		    gai_strerror(error));
		return;
	}
	res0 = res;
	do {
		s = socket(res0->ai_family, res0->ai_socktype,
		    res0->ai_protocol);
		if (s < 0) {
			VSL(SLT_Debug, 0, "Socket errno=%d", errno);
			continue;
		}
		error = connect(s, res0->ai_addr, res0->ai_addrlen);
		if (!error)
			break;
		VSL(SLT_Debug, 0, "Connect errno=%d", errno);
		close(s);
		s = -1;
	} while ((res0 = res0->ai_next) != NULL);
	freeaddrinfo(res);
	vc->fd = s;
	VSL(SLT_BackendOpen, vc->fd, "");
	return;
}

/*--------------------------------------------------------------------
 * When backend connections have been used, they are passed to us through
 * the vbe_pipe.  If fd == -1 it has been closed and will be reclaimed,
 * otherwise arm an event to monitor if the backend closes and recycle.
 */

static void
vbe_rdp(int fd, short event __unused, void *arg __unused)
{
	struct vbe_conn *vc;
	int i;

	i = read(fd, &vc, sizeof vc);
	assert(i == sizeof vc);
	AZ(pthread_mutex_lock(&vbemtx));
	TAILQ_REMOVE(&vc->vbe->bconn, vc, list);
	if (vc->fd < 0) {
		vc->vbe->nconn--;
		vbe_delete_conn(vc);
	} else {
		vc->inuse = 0;
		event_add(&vc->ev, NULL);
		TAILQ_INSERT_HEAD(&vc->vbe->fconn, vc, list);
	}
	AZ(pthread_mutex_unlock(&vbemtx));
}

/*--------------------------------------------------------------------
 * A backend connection became ready.  This can happen if it was reused
 * in which case we unarm the event and get out of the way, or if the 
 * backend closed the connection in which case we clean up.
 */

static void
vbe_rdf(int fd __unused, short event __unused, void *arg)
{
	struct vbe_conn *vc;
	int j;

	vc = arg;
	AZ(pthread_mutex_lock(&vbemtx));
	if (vc->inuse) {
		event_del(&vc->ev);
		AZ(pthread_mutex_unlock(&vbemtx));
		return;
	} 
	AZ(ioctl(vc->fd, FIONREAD, &j));
	VSL(SLT_BackendClose, vc->fd, "Remote (%d chars)", j);
	TAILQ_REMOVE(&vc->vbe->fconn, vc, list);
	AZ(pthread_mutex_unlock(&vbemtx));
	event_del(&vc->ev);
	close(vc->fd);
	vbe_delete_conn(vc);
}

/* Backend monitoring thread -----------------------------------------*/

static void *
vbe_main(void *priv __unused)
{
	struct event pev;

	vbe_evb = event_init();
	assert(vbe_evb != NULL);

	AZ(pipe(vbe_pipe));

	memset(&pev, 0, sizeof pev);
	event_set(&pev, vbe_pipe[0], EV_READ | EV_PERSIST, vbe_rdp, NULL);
	event_base_set(vbe_evb, &pev);
	event_add(&pev, NULL);

	event_base_loop(vbe_evb, 0);

	assert(__LINE__ == 0);
	return (NULL);
}

/* Get a backend connection ------------------------------------------
 *
 * First locate the backend shadow, if necessary by creating one.
 * If there are free connections, use the first, otherwise build a
 * new connection.
 */

struct vbe_conn *
VBE_GetFd(struct backend *bp, unsigned xid)
{
	struct vbe *vp;
	struct vbe_conn *vc;

	AZ(pthread_mutex_lock(&vbemtx));
	vp = bp->vbe;
	if (vp == NULL) {
		TAILQ_FOREACH(vp, &vbe_head, list)
			if (vp->ip == bp->ip)
				break;
	}
	if (vp == NULL) {
		vp = calloc(sizeof *vp, 1);
		assert(vp != NULL);
		TAILQ_INIT(&vp->fconn);
		TAILQ_INIT(&vp->bconn);
		vp->ip = bp->ip;
		bp->vbe = vp;
		TAILQ_INSERT_TAIL(&vbe_head, vp, list);
	}
	/* XXX: check nconn vs backend->maxcon */
	vc = TAILQ_FIRST(&vp->fconn);
	if (vc != NULL) {
		vc->inuse = 1;
		TAILQ_REMOVE(&vp->fconn, vc, list);
		TAILQ_INSERT_TAIL(&vp->bconn, vc, list);
		AZ(pthread_mutex_unlock(&vbemtx));
	} else {
		vc = vbe_new_conn();
		if (vc == NULL) {
			AZ(pthread_mutex_unlock(&vbemtx));
			return (NULL);
		}
		vp->nconn++;
		vc->vbe = vp;
		vc->fd = -1;
		vc->inuse = 1;
		TAILQ_INSERT_TAIL(&vp->bconn, vc, list);
		AZ(pthread_mutex_unlock(&vbemtx));
		connect_to_backend(vc, bp);
		if (vc->fd < 0) {
			AZ(pthread_mutex_lock(&vbemtx));
			TAILQ_REMOVE(&vc->vbe->bconn, vc, list);
			vp->nconn--;
			AZ(pthread_mutex_unlock(&vbemtx));
			vbe_delete_conn(vc);
			return (NULL);
		}
		VSL_stats->backend_conn++;
		event_set(&vc->ev, vc->fd,
		    EV_READ | EV_PERSIST, vbe_rdf, vc);
		event_base_set(vbe_evb, &vc->ev);
	}
	VSL(SLT_BackendXID, vc->fd, "%u", xid);
	return (vc);
}

/* Close a connection ------------------------------------------------*/

void
VBE_ClosedFd(struct vbe_conn *vc)
{
	int i;

	VSL(SLT_BackendClose, vc->fd, "");
	close(vc->fd);
	vc->fd = -1;
	i = write(vbe_pipe[1], &vc, sizeof vc);
	assert(i == sizeof vc);
}

/* Recycle a connection ----------------------------------------------*/

void
VBE_RecycleFd(struct vbe_conn *vc)
{
	int i;

	VSL_stats->backend_recycle++;
	VSL(SLT_BackendReuse, vc->fd, "");
	i = write(vbe_pipe[1], &vc, sizeof vc);
	assert(i == sizeof vc);
}

/*--------------------------------------------------------------------*/

void
VBE_Init(void)
{

	AZ(pthread_mutex_init(&vbemtx, NULL));
	AZ(pthread_create(&vbe_thread, NULL, vbe_main, NULL));
}
