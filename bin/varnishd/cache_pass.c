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
#include <event.h>

#include "libvarnish.h"
#include "shmlog.h"
#include "vcl_lang.h"
#include "cache.h"

static void
PassReturn(struct sess *sp)
{

	HERE();
	HttpdAnalyze(sp);
}

/*--------------------------------------------------------------------*/
void
PassSession(struct sess *sp)
{
	int fd, i;
	void *fd_token;
	struct sbuf *sb;
	struct event_base *eb;
	struct sess sp2;
	struct event ev;

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

	memset(&sp2, 0, sizeof sp2);
	memset(&ev, 0, sizeof ev);
	sp2.rd_e = &ev;
	sp2.fd = fd;
	eb = event_init();
	HttpdGetHead(&sp2, eb, PassReturn);
	event_base_loop(eb, 0);
}
