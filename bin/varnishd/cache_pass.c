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
		if (i <= 0) {
			vca_close_session(sp, "backend closed");
			return (1);
		}
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
	char *p, *q;
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
		xxxassert(i >= 0);
		if (i == 0 && p == bp)
			break;
		bp += i;
		/* buffer valid from p to bp */
		assert(bp >= p);

		/* chunk starts with f("%x\r\n", len) */
		u = strtoul(p, &q, 16);
		while (q && q < bp && *q == ' ')
			/* shouldn't happen - but sometimes it does */
			q++;
		if (q == NULL || q > bp - 2 /* want \r\n in same buffer */) {
			/* short - move to start of buffer and extend */
			memmove(buf, p, bp - p);
			bp -= p - buf;
			p = buf;
			continue;
		}
		assert(*q == '\r');
		q++;
		assert(*q == '\n');
		q++;

		/* we just received the final zero-length chunk */
		if (u == 0) {
			sp->wrk->acct.bodybytes += WRK_Write(sp->wrk, p, q - p);
			break;
		}

		/* include chunk header */
		u += q - p;

		/* include trailing \r\n with chunk */
		u += 2;

		for (;;) {
			j = u;
			if (bp - p < j)
				j = bp - p;
			sp->wrk->acct.bodybytes += WRK_Write(sp->wrk, p, j);
			WRK_Flush(sp->wrk);
			p += j;
			assert(u >= j);
			u -= j;
			if (u == 0)
				break;
			p = bp = buf;
			j = u;
			if (j > be - bp)
				j = be - bp;
			i = http_Read(hp, fd, bp, j);
			xxxassert(i > 0);
			bp += i;
		}
	}
	if (WRK_Flush(sp->wrk))
		vca_close_session(sp, "remote closed");
	return (0);
}


/*--------------------------------------------------------------------*/

void
PassBody(struct sess *sp)
{
	struct vbe_conn *vc;
	char *b;
	int cls;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->vbc, VBE_CONN_MAGIC);
	vc = sp->vbc;
	sp->vbc = NULL;

	clock_gettime(CLOCK_REALTIME, &sp->t_resp);

	http_ClrHeader(sp->http);
	http_CopyResp(sp->wrk, sp->fd, sp->http, vc->http);
	http_FilterHeader(sp->wrk, sp->fd, sp->http, vc->http, HTTPH_A_PASS);
	http_PrintfHeader(sp->wrk, sp->fd, sp->http, "X-Varnish: %u", sp->xid);
	/* XXX */
	if (http_HdrIs(vc->http, H_Transfer_Encoding, "chunked"))
		http_PrintfHeader(sp->wrk, sp->fd, sp->http, "Transfer-Encoding: chunked");
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
		VBE_ClosedFd(sp->wrk, vc, 0);
	else
		VBE_RecycleFd(sp->wrk, vc);
}


/*--------------------------------------------------------------------*/

int
PassSession(struct sess *sp)
{
	int i;
	struct vbe_conn *vc;
	struct worker *w;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->wrk, WORKER_MAGIC);
	w = sp->wrk;

	vc = VBE_GetFd(sp);
	if (vc == NULL)
		return (1);

	http_CopyReq(w, vc->fd, vc->http, sp->http);
	http_FilterHeader(w, vc->fd, vc->http, sp->http, HTTPH_R_PASS);
	http_PrintfHeader(w, vc->fd, vc->http, "X-Varnish: %u", sp->xid);
	WRK_Reset(w, &vc->fd);
	http_Write(w, vc->http, 0);
	i = WRK_Flush(w);
	xxxassert(i == 0);

	/* XXX: copy any contents */

	i = http_RecvHead(vc->http, vc->fd);
	xxxassert(i == 0);
	http_DissectResponse(w, vc->http, vc->fd);

	assert(sp->vbc == NULL);
	sp->vbc = vc;
	return (0);
}
