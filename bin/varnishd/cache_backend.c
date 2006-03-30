/*
 * $Id$
 */

#include <assert.h>
#include <stdlib.h>
#include <sys/queue.h>
#include "libvarnish.h"
#include "vcl_lang.h"

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

/*--------------------------------------------------------------------*/
void
connect_to_backend(struct vbe_conn *vc, struct backend *bp)
{
}

/*--------------------------------------------------------------------*/

int
VBE_GetFd(struct backend *bp)
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
		return (vc->fd);
	}
	vc = calloc(sizeof *vc, 1);
	assert(vc != NULL);
	vc->vbe = vp;
	TAILQ_INSERT_TAIL(&vp->bconn, vc, list);
	AZ(pthread_mutex_unlock(&vbemtx));
	connect_to_backend(vc, bp);

	/* XXX */
	return (-1);
}




void
VBE_Init(void)
{

	AZ(pthread_mutex_init(&vbemtx, NULL));
}
