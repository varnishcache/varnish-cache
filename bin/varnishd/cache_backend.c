/*
 * $Id$
 */

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sbuf.h>

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

void
VBE_ClosedFd(void *ptr)
{
	struct vbe_conn *vc;

	vc = ptr;
	AZ(pthread_mutex_lock(&vbemtx));
	TAILQ_REMOVE(&vc->vbe->bconn, vc, list);
	AZ(pthread_mutex_unlock(&vbemtx));
	free(vc);
}

/*--------------------------------------------------------------------*/
void
VBE_Pass(struct sess *sp)
{
	int fd, i;
	void *fd_token;
	struct sbuf *sb;

	fd = VBE_GetFd(sp->backend, &fd_token);
	assert(fd != -1);

	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	assert(sb != NULL);
	sbuf_cat(sb, sp->http.req);
	sbuf_cat(sb, " ");
	sbuf_cat(sb, sp->http.url);
	sbuf_cat(sb, " ");
	sbuf_cat(sb, sp->http.proto);
	sbuf_cat(sb, "\r\n");
#define HTTPH(a, b, c, d, e, f, g) 				\
	do {							\
		if (c && sp->http.b != NULL) {			\
			sbuf_cat(sb, a ": ");			\
			sbuf_cat(sb, sp->http.b);		\
			sbuf_cat(sb, "\r\n");			\
		}						\
	} while (0);
#include "http_headers.h"
#undef HTTPH
	sbuf_cat(sb, "\r\n");
	sbuf_finish(sb);
	printf("REQ: <%s>\n", sbuf_data(sb));
	i = write(fd, sbuf_data(sb), sbuf_len(sb));
	assert(i == sbuf_len(sb));
	{
	char buf[101];

	for(;;) {
		i = read(fd, buf, 100);
		if (i > 0) {
			buf[i] = '\0';
			printf("RESP: <%s>\n", buf);
		}
	} 

	}
}

/*--------------------------------------------------------------------*/

void
VBE_Init(void)
{

	AZ(pthread_mutex_init(&vbemtx, NULL));
}
