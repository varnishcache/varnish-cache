/*
 * $Id$
 *
 * XXX: charge bytes to srcaddr
 */

#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "shmlog.h"
#include "cache.h"

#define			PASS_BUFSIZ		8192

/*--------------------------------------------------------------------*/

static int
pass_straight(struct sess *sp, int fd, struct http *hp, char *bi)
{
	int i;
	off_t	cl;
	unsigned c;
	char buf[PASS_BUFSIZ];

	if (bi != NULL)
		cl = strtoumax(bi, NULL, 0);
	else
		cl = (1 << 30);

	i = fcntl(fd, F_GETFL);		/* XXX ? */
	i &= ~O_NONBLOCK;
	i = fcntl(fd, F_SETFL, i);

	while (cl != 0) {
		c = cl;
		if (c > sizeof buf)
			c = sizeof buf;
		i = http_Read(hp, fd, buf, c);
		if (i == 0 && bi == NULL)
			return (1);
		assert(i > 0);
		sp->wrk->acct.bodybytes += WRK_Write(sp->wrk, buf, i);
		if (WRK_Flush(sp->wrk))
			vca_close_session(sp, "remote closed");
		cl -= i;
	}
	return (0);
}


/*--------------------------------------------------------------------*/

static int
pass_chunked(struct sess *sp, int fd, struct http *hp)
{
	int i, j;
	char *b, *q, *e;
	char *p;
	unsigned u;
	char buf[PASS_BUFSIZ];
	char *bp, *be;

	i = fcntl(fd, F_GETFL);		/* XXX ? */
	i &= ~O_NONBLOCK;
	i = fcntl(fd, F_SETFL, i);

	bp = buf;
	be = buf + sizeof buf;
	p = buf;
	while (1) {
		i = http_Read(hp, fd, bp, be - bp);
		i = read(fd, bp, be - bp);
		assert(i > 0);
		bp += i;
		/* buffer valid from p to bp */

		u = strtoul(p, &q, 16);
		if (q == NULL || (*q != '\n' && *q != '\r')) {
			INCOMPL();
			/* XXX: move bp to buf start, get more */
		}
		if (*q == '\r')
			q++;
		assert(*q == '\n');
		q++;
		if (u == 0)
			break;

		sp->wrk->acct.bodybytes += WRK_Write(sp->wrk, p, q - p);

		p = q;

		while (u > 0) {
			j = u;
			if (bp == p) {
				bp = p = buf;
				break;
			}
			if (bp - p < j)
				j = bp - p;
			sp->wrk->acct.bodybytes += WRK_Write(sp->wrk, p, j);
			p += j;
			u -= j;
		}
		while (u > 0) {
			if (http_GetTail(hp, u, &b, &e)) {
				j = e - b;
				sp->wrk->acct.bodybytes +=
				    WRK_Write(sp->wrk, q, j);
				u -= j;
			} else
				break;
		}
		if (WRK_Flush(sp->wrk))
			vca_close_session(sp, "remote closed");
		while (u > 0) {
			j = u;
			if (j > sizeof buf)
				j = sizeof buf;
			i = read(fd, buf, j);
			assert(i > 0);
			sp->wrk->acct.bodybytes += WRK_Write(sp->wrk, buf, i);
			u -= i;
			if (WRK_Flush(sp->wrk))
				vca_close_session(sp, "remote closed");
		}
	}
	return (0);
}


/*--------------------------------------------------------------------*/

void
PassBody(struct sess *sp)
{
	struct vbe_conn *vc;
	char *b;
	int cls;

	vc = sp->vbc;
	assert(vc != NULL);

	http_ClrHeader(sp->http);
	http_CopyResp(sp->fd, sp->http, vc->http);
	http_FilterHeader(sp->fd, sp->http, vc->http, HTTPH_A_PASS);
	http_PrintfHeader(sp->fd, sp->http, "X-Varnish: %u", sp->xid);
	WRK_Reset(sp->wrk, &sp->fd);
	sp->wrk->acct.hdrbytes += http_Write(sp->wrk, sp->http, 1);

	if (http_GetHdr(vc->http, H_Content_Length, &b))
		cls = pass_straight(sp, vc->fd, vc->http, b);
	else if (http_HdrIs(vc->http, H_Connection, "close"))
		cls = pass_straight(sp, vc->fd, vc->http, NULL);
	else if (http_HdrIs(vc->http, H_Transfer_Encoding, "chunked"))
		cls = pass_chunked(sp, vc->fd, vc->http);
	else {
		cls = pass_straight(sp, vc->fd, vc->http, NULL);
	}

	if (WRK_Flush(sp->wrk))
		vca_close_session(sp, "remote closed");

	if (http_GetHdr(vc->http, H_Connection, &b) && !strcasecmp(b, "close"))
		cls = 1;

	if (cls)
		VBE_ClosedFd(vc);
	else
		VBE_RecycleFd(vc);
}


/*--------------------------------------------------------------------*/

void
PassSession(struct sess *sp)
{
	int i;
	struct vbe_conn *vc;
	struct worker *w;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	w = sp->wrk;

	vc = VBE_GetFd(sp->backend, sp->xid);
	assert(vc != NULL);
	VSL(SLT_Backend, sp->fd, "%d %s", vc->fd, sp->backend->vcl_name);

	http_CopyReq(vc->fd, vc->http, sp->http);
	http_FilterHeader(vc->fd, vc->http, sp->http, HTTPH_R_PASS);
	http_PrintfHeader(vc->fd, vc->http, "X-Varnish: %u", sp->xid);
	WRK_Reset(w, &vc->fd);
	http_Write(w, vc->http, 0);
	i = WRK_Flush(w);
	assert(i == 0);

	/* XXX: copy any contents */

	i = http_RecvHead(vc->http, vc->fd);
	assert(i == 0);
	http_DissectResponse(vc->http, vc->fd);

	sp->vbc = vc;
}
