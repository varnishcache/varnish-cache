/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <assert.h>
#include <netdb.h>
#include <pthread.h>
#include <queue.h>
#include <sbuf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "vcl_lang.h"
#include "cache.h"

/*
 * The internal backend structure for managing connection pools per
 * backend.  We need to shadow the backend stucture from the VCL
 * in order let connections live across VCL switches.
 */

struct vbe_conn {
	TAILQ_ENTRY(vbe_conn)	list;
	struct vbe		*vbe;
	int			fd;
};

struct vbe {
	unsigned		ip;
	TAILQ_ENTRY(vbe)	list;
	TAILQ_HEAD(,vbe_conn)	fconn;
	TAILQ_HEAD(,vbe_conn)	bconn;
	unsigned		nconn;
};

static TAILQ_HEAD(,vbe) vbe_head = TAILQ_HEAD_INITIALIZER(vbe_head);

static pthread_mutex_t	vbemtx;

/*--------------------------------------------------------------------
 * XXX: we should not call getaddrinfo() every time, we should cache
 * and apply round-robin with blacklisting of entries that do not respond
 * etc.  Periodic re-lookups to capture changed DNS records would also 
 * be a good thing in that case.
 */

void
connect_to_backend(struct vbe_conn *vc, struct backend *bp)
{
	struct addrinfo *res, *res0, hint;
	int error, s;

	assert(bp != NULL);
	assert(bp->hostname != NULL);
	memset(&hint, 0, sizeof hint);
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(bp->hostname,
	    bp->portname == NULL ? "http" : bp->portname,
	    &hint, &res);
	if (error) {
		fprintf(stderr, "getaddrinfo: %s\n", 
		    gai_strerror(error));
		return;
	}
	res0 = res;
	do {
		s = socket(res0->ai_family, res0->ai_socktype,
		    res0->ai_protocol);
		if (s < 0)
			continue;
		error = connect(s, res0->ai_addr, res0->ai_addrlen);
		if (!error)
			break;
		close(s);
		s = -1;
	} while ((res0 = res0->ai_next) != NULL);
	freeaddrinfo(res);
	vc->fd = s;
	VSL(SLT_BackendOpen, vc->fd, "");
	return;
}

/*--------------------------------------------------------------------*/

int
VBE_GetFd(struct backend *bp, void **ptr)
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
		TAILQ_REMOVE(&vp->fconn, vc, list);
		TAILQ_INSERT_TAIL(&vp->bconn, vc, list);
		AZ(pthread_mutex_unlock(&vbemtx));
	} else {
		vc = calloc(sizeof *vc, 1);
		assert(vc != NULL);
		vc->vbe = vp;
		vc->fd = -1;
		TAILQ_INSERT_TAIL(&vp->bconn, vc, list);
		AZ(pthread_mutex_unlock(&vbemtx));
		connect_to_backend(vc, bp);
	}
	*ptr = vc;
	return (vc->fd);
}

/*--------------------------------------------------------------------*/

void
VBE_ClosedFd(void *ptr)
{
	struct vbe_conn *vc;

	vc = ptr;
	VSL(SLT_BackendClose, vc->fd, "");
	close(vc->fd);
	AZ(pthread_mutex_lock(&vbemtx));
	TAILQ_REMOVE(&vc->vbe->bconn, vc, list);
	AZ(pthread_mutex_unlock(&vbemtx));
	free(vc);
}

/*--------------------------------------------------------------------*/

void
VBE_RecycleFd(void *ptr)
{
	struct vbe_conn *vc;

	vc = ptr;
	VSL(SLT_BackendReuse, vc->fd, "");
	AZ(pthread_mutex_lock(&vbemtx));
	TAILQ_REMOVE(&vc->vbe->bconn, vc, list);
	TAILQ_INSERT_HEAD(&vc->vbe->fconn, vc, list);
	AZ(pthread_mutex_unlock(&vbemtx));
}

/*--------------------------------------------------------------------*/

void
VBE_Init(void)
{

	AZ(pthread_mutex_init(&vbemtx, NULL));
}
