/*
 * $Id$
 *
 * Manage backend connections.
 *
 * XXX: When we switch VCL we can have vbe_conn's dangling from
 * XXX: the backends no longer used.  When the VCL's refcount
 * XXX: drops to zero we should zap them.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/ioctl.h>

#include "heritage.h"
#include "shmlog.h"
#include "cache.h"

/* A backend IP */

static TAILQ_HEAD(,vbe_conn) vbe_head = TAILQ_HEAD_INITIALIZER(vbe_head);

static pthread_mutex_t	vbemtx;

/*--------------------------------------------------------------------*/

static struct vbe_conn *
vbe_new_conn(void)
{
	struct vbe_conn *vbc;
	unsigned char *p;

	vbc = calloc(sizeof *vbc + params->mem_workspace * 2, 1);
	if (vbc == NULL)
		return (NULL);
	VSL_stats->n_vbe_conn++;
	vbc->magic = VBE_CONN_MAGIC;
	vbc->http = &vbc->http_mem[0];
	vbc->http2 = &vbc->http_mem[1];
	vbc->fd = -1;
	p = (void *)(vbc + 1);
	http_Setup(vbc->http, p, params->mem_workspace);
	p += params->mem_workspace;
	http_Setup(vbc->http2, p, params->mem_workspace);
	return (vbc);
}

/*--------------------------------------------------------------------*/

static void
vbe_lookup(struct backend *bp)
{
	struct addrinfo *res, hint;
	int error;

	if (bp->addr != NULL)
		freeaddrinfo(bp->addr);

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
		printf("getaddrinfo: %s\n", gai_strerror(error)); /* XXX */
		bp->addr = NULL;
		return;
	}
	bp->addr = res;
}

/*--------------------------------------------------------------------*/

static int
vbe_sock_conn(const struct addrinfo *ai)
{
	int s;

	s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (s < 0) 
		return (s);
	else if (connect(s, ai->ai_addr, ai->ai_addrlen)) {
		AZ(close(s));
		s = -1;
	} 
	return (s);
}

/*--------------------------------------------------------------------*/

static int
vbe_conn_try(struct backend *bp, struct addrinfo **pai)
{
	struct addrinfo *ai;
	int s;

	/* First try the cached good address, and any following it */
	for (ai = bp->last_addr; ai != NULL; ai = ai->ai_next) {
		s = vbe_sock_conn(ai);
		if (s >= 0) {
			bp->last_addr = ai;
			*pai = ai;
			return (s);
		}
	}

	/* Then try the list until the cached last good address */
	for (ai = bp->addr; ai != bp->last_addr; ai = ai->ai_next) {
		s = vbe_sock_conn(ai);
		if (s >= 0) {
			bp->last_addr = ai;
			*pai = ai;
			return (s);
		}
	}

	/* Then do another lookup to catch DNS changes */
	vbe_lookup(bp);

	/* And try the entire list */
	for (ai = bp->addr; ai != NULL; ai = ai->ai_next) {
		s = vbe_sock_conn(ai);
		if (s >= 0) {
			bp->last_addr = ai;
			*pai = ai;
			return (s);
		}
	}

	return (-1);
}

static int
vbe_connect(struct backend *bp)
{
	int s;
	char abuf1[TCP_ADDRBUFSIZE], abuf2[TCP_ADDRBUFSIZE];
	char pbuf1[TCP_PORTBUFSIZE], pbuf2[TCP_PORTBUFSIZE];
	struct addrinfo *ai;

	assert(bp != NULL);
	assert(bp->hostname != NULL);

	s = vbe_conn_try(bp, &ai);
	if (s < 0) 
		return (s);

	TCP_myname(s, abuf1, sizeof abuf1, pbuf1, sizeof pbuf1);
	TCP_name(ai->ai_addr, ai->ai_addrlen,
	    abuf2, sizeof abuf2, pbuf2, sizeof pbuf2);
	VSL(SLT_BackendOpen, s, "%s %s %s %s %s",
	    bp->vcl_name, abuf1, pbuf1, abuf2, pbuf2);
	return (s);
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
	struct vbe_conn *vc, *vc2;
	struct pollfd pfd;

	CHECK_OBJ_NOTNULL(bp, BACKEND_MAGIC);
	while (1) {
		/*
		 * Try all connections on this backend until we find one
		 * that works.  If that fails, grab a free connection
		 * (if any) while we have the lock anyway.
		 */
		vc2 = NULL;
		AZ(pthread_mutex_lock(&vbemtx));
		vc = TAILQ_FIRST(&bp->connlist);
		if (vc != NULL) {
			assert(vc->fd >= 0);
			TAILQ_REMOVE(&bp->connlist, vc, list);
		} else {
			vc2 = TAILQ_FIRST(&vbe_head);
			if (vc2 != NULL) {
				VSL_stats->backend_unused--;
				TAILQ_REMOVE(&vbe_head, vc2, list);
			}
		}
		AZ(pthread_mutex_unlock(&vbemtx));
		if (vc == NULL)
			break;

		/* Test the connection for remote close before we use it */
		pfd.fd = vc->fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (!poll(&pfd, 1, 0))
			break;
		VBE_ClosedFd(vc);
	}

	if (vc == NULL) {
		if (vc2 == NULL)
			vc = vbe_new_conn();
		else
			vc = vc2;
		assert(vc != NULL);
		assert(vc->fd == -1);
		assert(vc->backend == NULL);
	}

	/* If not connected yet, do so */
	if (vc->fd < 0) {
		assert(vc->backend == NULL);
		vc->fd = vbe_connect(bp);
		AZ(pthread_mutex_lock(&vbemtx));
		if (vc->fd < 0) {
			vc->backend = NULL;
			TAILQ_INSERT_HEAD(&vbe_head, vc, list);
			VSL_stats->backend_unused++;
			vc = NULL;
		} else {
			vc->backend = bp;
		}
		AZ(pthread_mutex_unlock(&vbemtx));
	} else {
		assert(vc->fd >= 0);
		assert(vc->backend == bp);
	}
	if (vc != NULL ) {
		assert(vc->fd >= 0);
		VSL_stats->backend_conn++;
		VSL(SLT_BackendXID, vc->fd, "%u", xid);
		assert(vc->backend != NULL);
	}
	return (vc);
}

/* Close a connection ------------------------------------------------*/

void
VBE_ClosedFd(struct vbe_conn *vc)
{

	CHECK_OBJ_NOTNULL(vc, VBE_CONN_MAGIC);
	assert(vc->fd >= 0);
	assert(vc->backend != NULL);
	VSL(SLT_BackendClose, vc->fd, "%s", vc->backend->vcl_name);
	AZ(close(vc->fd));
	vc->fd = -1;
	vc->backend = NULL;
	AZ(pthread_mutex_lock(&vbemtx));
	TAILQ_INSERT_HEAD(&vbe_head, vc, list);
	VSL_stats->backend_unused++;
	AZ(pthread_mutex_unlock(&vbemtx));
}

/* Recycle a connection ----------------------------------------------*/

void
VBE_RecycleFd(struct vbe_conn *vc)
{

	CHECK_OBJ_NOTNULL(vc, VBE_CONN_MAGIC);
	assert(vc->fd >= 0);
	assert(vc->backend != NULL);
	VSL_stats->backend_recycle++;
	VSL(SLT_BackendReuse, vc->fd, "%s", vc->backend->vcl_name);
	AZ(pthread_mutex_lock(&vbemtx));
	TAILQ_INSERT_HEAD(&vc->backend->connlist, vc, list);
	AZ(pthread_mutex_unlock(&vbemtx));
}

/*--------------------------------------------------------------------*/

void
VBE_Init(void)
{

	AZ(pthread_mutex_init(&vbemtx, NULL));
}
