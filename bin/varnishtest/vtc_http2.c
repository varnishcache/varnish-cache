/*-
 * Copyright (c) 2008-2016 Varnish Software AS
 * All rights reserved.
 *
 * Author: Guillaume Quintard <guillaume.quintard@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>

#include "vtc.h"
#include "vtc_http.h"

#include "vfil.h"
#include "hpack.h"
#include "vend.h"

#define ERR_MAX		13
#define BUF_SIZE	(1024*2048)

static const char *const h2_errs[] = {
#define H2_ERROR(n,v,sc,t) [v] = #n,
#include <tbl/h2_error.h>
	NULL
};

static const char *const h2_types[] = {
#define H2_FRAME(l,u,t,f,...) [t] = #u,
#include <tbl/h2_frames.h>
	NULL
};

#define SETTINGS_MAX 0x06

static const char * const h2_settings[] = {
	[0] = "unknown",
#define H2_SETTING(U,l,v,...) [v] = #U,
#include <tbl/h2_settings.h>
	NULL
};

enum h2_type {
#define H2_FRAME(l,u,t,f,...) TYPE_##u = t,
#include <tbl/h2_frames.h>
	TYPE_MAX
};

enum {
	ACK = 0x1,
	END_STREAM = 0x1,
	PADDED = 0x8,
	END_HEADERS = 0x4,
	PRIORITY = 0x20,
};

struct stream {
	unsigned		magic;
#define STREAM_MAGIC		0x63f1fac2
	uint32_t		id;
	char			*spec;
	char			*name;
	VTAILQ_ENTRY(stream)    list;
	unsigned		running;
	pthread_cond_t		cond;
	struct frame		*frame;
	pthread_t		tp;
	struct http		*hp;
	int64_t			ws;
	int			wf;

	VTAILQ_HEAD(, frame)   fq;

	char			*body;
	int			bodylen;
	struct hpk_hdr		req[MAX_HDR];
	struct hpk_hdr		resp[MAX_HDR];

	int			dependency;
	int			weight;
};

static void
clean_headers(struct hpk_hdr *h)
{
	while (h->t) {
		if (h->key.len)
			free(h->key.ptr);
		if (h->value.len)
			free(h->value.ptr);
		memset(h, 0, sizeof(*h));
		h++;
	}
}

#define ONLY_CLIENT(hp, av)						\
	do {								\
		if (hp->sfd != NULL)					\
			vtc_fatal(hp->vl,				\
			    "\"%s\" only possible in client", av[0]);	\
	} while (0)

#define ONLY_SERVER(hp, av)						\
	do {								\
		if (hp->sfd == NULL)					\
			vtc_fatal(hp->vl,				\
			    "\"%s\" only possible in server", av[0]);	\
	} while (0)

static void
http_write(const struct http *hp, int lvl,
    const char *buf, int s, const char *pfx)
{
	ssize_t l;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	AN(buf);
	AN(pfx);

	vtc_dump(hp->vl, lvl, pfx, buf, s);
	l = write(hp->fd, buf, s);
	if (l != s)
		vtc_log(hp->vl, hp->fatal, "Write failed: (%zd vs %d) %s",
		    l, s, strerror(errno));
}

static int
get_bytes(const struct http *hp, char *buf, int n)
{
	int i;
	struct pollfd pfd[1];

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	AN(buf);

	while (n > 0) {
		pfd[0].fd = hp->fd;
		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		i = poll(pfd, 1, hp->timeout);
		if (i < 0 && errno == EINTR)
			continue;
		if (i == 0)
			vtc_log(hp->vl, 3,
			    "HTTP2 rx timeout (fd:%d %u ms)",
			    hp->fd, hp->timeout);
		if (i < 0)
			vtc_log(hp->vl, 3,
			    "HTTP2 rx failed (fd:%d poll: %s)",
			    hp->fd, strerror(errno));
		if (i <= 0)
			return (i);
		i = read(hp->fd, buf, n);
		if (!(pfd[0].revents & POLLIN))
			vtc_log(hp->vl, 4,
			    "HTTP2 rx poll (fd:%d revents: %x n=%d, i=%d)",
			    hp->fd, pfd[0].revents, n, i);
		if (i == 0)
			vtc_log(hp->vl, 3,
			    "HTTP2 rx EOF (fd:%d read: %s)",
			    hp->fd, strerror(errno));
		if (i < 0)
			vtc_log(hp->vl, 3,
			    "HTTP2 rx failed (fd:%d read: %s)",
			    hp->fd, strerror(errno));
		if (i <= 0)
			return (i);
		n -= i;
	}
	return (1);

}

VTAILQ_HEAD(fq_head, frame);

struct frame {
	unsigned	magic;
#define	FRAME_MAGIC	0x5dd3ec4
	uint32_t	size;
	uint32_t	stid;
	uint8_t		type;
	uint8_t		flags;
	char		*data;

	VTAILQ_ENTRY(frame)    list;

	union {
		struct {
			uint32_t stream;
			uint8_t  exclusive;
			uint8_t  weight;
		}		prio;
		uint32_t	rst_err;
		double settings[SETTINGS_MAX+1];
		struct {
			char data[9];
			int ack;
		}		ping;
		struct {
			uint32_t err;
			uint32_t stream;
			char	 *debug;
		}		goaway;
		uint32_t	winup_size;
		uint32_t	promised;
		uint8_t		padded;
	} md;
};

static void
readFrameHeader(struct frame *f, const char *buf)
{
	CHECK_OBJ_NOTNULL(f, FRAME_MAGIC);
	AN(buf);

	f->size  = (unsigned char)buf[0] << 16;
	f->size += (unsigned char)buf[1] << 8;
	f->size += (unsigned char)buf[2];

	f->type = (unsigned char)buf[3];

	f->flags = (unsigned char)buf[4];

	f->stid  = (0xff & (unsigned char)buf[5]) << 24;
	f->stid += (0xff & (unsigned char)buf[6]) << 16;
	f->stid += (0xff & (unsigned char)buf[7]) <<  8;
	f->stid += (0xff & (unsigned char)buf[8]);
}

static void
writeFrameHeader(char *buf, const struct frame *f)
{
	CHECK_OBJ_NOTNULL(f, FRAME_MAGIC);
	AN(buf);
	buf[0] = (f->size >> 16) & 0xff;
	buf[1] = (f->size >>  8) & 0xff;
	buf[2] = (f->size      ) & 0xff;

	buf[3] = f->type;

	buf[4] = f->flags;

	buf[5] = (f->stid >> 24) & 0xff;
	buf[6] = (f->stid >> 16) & 0xff;
	buf[7] = (f->stid >>  8) & 0xff;
	buf[8] = (f->stid      ) & 0xff;
}

#define INIT_FRAME(f, ty, sz, id, fl) \
do { \
	f.magic = FRAME_MAGIC; \
	f.type = TYPE_ ## ty; \
	f.size = sz; \
	f.stid = id; \
	f.flags = fl; \
	f.data = NULL; \
} while(0)

static void
clean_frame(struct frame **f)
{
	AN(f);
	if (!*f)
		return;

	CHECK_OBJ_NOTNULL(*f, FRAME_MAGIC);

	if ((*f)->type == TYPE_GOAWAY)
		free((*f)->md.goaway.debug);
	free((*f)->data);
	free(*f);
	*f = NULL;
}

static void
write_frame(struct http *hp, const struct frame *f, const unsigned lock)
{
	ssize_t l;
	char hdr[9];

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(f, FRAME_MAGIC);

	writeFrameHeader(hdr, f);

	vtc_log(hp->vl, 3, "tx: stream: %d, type: %s (%d), "
			"flags: 0x%02x, size: %d",
			f->stid,
			f->type < TYPE_MAX ? h2_types[f->type] : "?",
			f->type, f->flags, f->size);

	if (lock)
		AZ(pthread_mutex_lock(&hp->mtx));
	l = write(hp->fd, hdr, sizeof(hdr));
	if (l != sizeof(hdr))
		vtc_log(hp->vl, hp->fatal, "Write failed: (%zd vs %zd) %s",
		    l, sizeof(hdr), strerror(errno));

	if (f->size) {
		AN(f->data);
		l = write(hp->fd, f->data, f->size);
		if (l != f->size)
			vtc_log(hp->vl, hp->fatal,
					"Write failed: (%zd vs %d) %s",
					l, f->size, strerror(errno));
	}
	if (lock)
		AZ(pthread_mutex_unlock(&hp->mtx));
}

static void
exclusive_stream_dependency(const struct stream *s)
{
	struct stream *target;
	struct http *hp = s->hp;

	if (s->id == 0)
		return;

	VTAILQ_FOREACH(target, &hp->streams, list) {
		if (target->id != s->id && target->dependency == s->dependency)
			target->dependency = s->id;
	}
}

static void
explain_flags(uint8_t flags, uint8_t type, struct vtclog *vl)
{
	if (flags & ACK && (type == TYPE_PING || type == TYPE_SETTINGS)) {
		vtc_log(vl, 3, "flag: ACK");
	} else if (flags & END_STREAM && (type == TYPE_HEADERS ||
	    type == TYPE_PUSH_PROMISE || type == TYPE_DATA)) {
		vtc_log(vl, 3, "flag: END_STREAM");
	} else if (flags & END_HEADERS && (type == TYPE_HEADERS ||
	    type == TYPE_PUSH_PROMISE || type == TYPE_CONTINUATION)) {
		vtc_log(vl, 3, "flag: END_TYPE_HEADERS");
	} else if (flags & PRIORITY && (type == TYPE_HEADERS ||
	    type == TYPE_PUSH_PROMISE)) {
		vtc_log(vl, 3, "flag: END_PRIORITY");
	} else if (flags & PADDED && (type == TYPE_DATA || type ==
	    TYPE_HEADERS || type == TYPE_PUSH_PROMISE)) {
		vtc_log(vl, 3, "flag: PADDED");
	} else if (flags)
		vtc_log(vl, 3, "UNKNOWN FLAG(S): 0x%02x", flags);
}

static void
parse_data(struct stream *s, struct frame *f)
{
	struct http *hp;
	uint32_t size = f->size;
	char *data = f->data;
	CHECK_OBJ_NOTNULL(f, FRAME_MAGIC);
	CHECK_OBJ_NOTNULL(s, STREAM_MAGIC);
	CAST_OBJ_NOTNULL(hp, s->hp, HTTP_MAGIC);;

	if (f->flags & PADDED) {
		f->md.padded = *((uint8_t *)data);
		if (f->md.padded >= size) {
			vtc_log(hp->vl, hp->fatal,
					"invalid padding: %d reported,"
					"but size is only %d",
					f->md.padded, size);
			size = 0;
			f->md.padded = 0;
		}
		data++;
		size -= f->md.padded + 1;
		vtc_log(hp->vl, 4, "padding: %3d", f->md.padded);
	}

	if (s->id)
		s->ws -= size;

	s->hp->ws -= size;

	if (!size) {
		AZ(data);
		vtc_log(hp->vl, 4, "s%u - no data", s->id);
		return;
	}

	if (s->body) {
		s->body = realloc(s->body, s->bodylen + size + 1L);
	} else {
		AZ(s->bodylen);
		s->body = malloc(size + 1L);
	}
	AN(s->body);
	memcpy(s->body + s->bodylen, data, size);
	s->bodylen += size;
	s->body[s->bodylen] = '\0';
}

static void
decode_hdr(struct http *hp, struct hpk_hdr *h, const struct vsb *vsb)
{
	struct hpk_iter *iter;
	enum hpk_result r = hpk_err;
	int n;

	CHECK_OBJ_NOTNULL(vsb, VSB_MAGIC);
	CAST_OBJ_NOTNULL(hp, hp, HTTP_MAGIC);;

	if (VSB_len(vsb) == 0)
		return;

	iter = HPK_NewIter(hp->decctx, VSB_data(vsb), VSB_len(vsb));

	n = 0;
	while (n < MAX_HDR && h[n].t)
		n++;
	while (n < MAX_HDR) {
		r = HPK_DecHdr(iter, h + n);
		if (r == hpk_err )
			break;
		vtc_log(hp->vl, 4,
				"header[%2d]: %s : %s",
				n,
				h[n].key.ptr,
				h[n].value.ptr);
		n++;
		if (r == hpk_done)
			break;
	}

	if (r != hpk_done)
		vtc_log(hp->vl, hp->fatal ? 4 : 0,
				"Header decoding failed (%d) %d", r, hp->fatal);
	else if (n == MAX_HDR)
		vtc_log(hp->vl, hp->fatal,
				"Max number of headers reached (%d)", MAX_HDR);

	HPK_FreeIter(iter);
}

static void
parse_hdr(struct stream *s, struct frame *f, struct vsb *vsb)
{
	int shift = 0;
	int exclusive = 0;
	uint32_t size = f->size;
	char *data = f->data;
	struct http *hp;
	uint32_t n;

	CHECK_OBJ_NOTNULL(f, FRAME_MAGIC);
	CHECK_OBJ_NOTNULL(s, STREAM_MAGIC);
	CHECK_OBJ_NOTNULL(vsb, VSB_MAGIC);
	CAST_OBJ_NOTNULL(hp, s->hp, HTTP_MAGIC);;

	if (f->flags & PADDED && f->type != TYPE_CONTINUATION) {
		f->md.padded = *((uint8_t *)data);
		if (f->md.padded >= size) {
			vtc_log(hp->vl, hp->fatal,
					"invalid padding: %d reported,"
					"but size is only %d",
					f->md.padded, size);
			size = 0;
			f->md.padded = 0;
		}
		shift += 1;
		size -= f->md.padded;
		vtc_log(hp->vl, 4, "padding: %3d", f->md.padded);
	}

	if (f->type == TYPE_HEADERS && f->flags & PRIORITY){
		shift += 5;
		n = vbe32dec(f->data);
		s->dependency = n & ~(1U << 31);
		exclusive = n >> 31;

		s->weight = f->data[4];
		if (exclusive)
			exclusive_stream_dependency(s);

		vtc_log(hp->vl, 4, "stream->dependency: %u", s->dependency);
		vtc_log(hp->vl, 4, "stream->weight: %u", s->weight);
	} else if (f->type == TYPE_PUSH_PROMISE){
		shift += 4;
		n = vbe32dec(f->data);
		f->md.promised = n & ~(1U << 31);
	}

	AZ(VSB_bcat(vsb, data + shift, size - shift));
}

static void
parse_prio(struct stream *s, struct frame *f)
{
	struct http *hp;
	char *buf;
	uint32_t n;

	CHECK_OBJ_NOTNULL(f, FRAME_MAGIC);
	CHECK_OBJ_NOTNULL(s, STREAM_MAGIC);
	CAST_OBJ_NOTNULL(hp, s->hp, HTTP_MAGIC);;

	if (f->size != 5)
		vtc_fatal(hp->vl, "Size should be 5, but isn't (%d)", f->size);

	buf = f->data;
	AN(buf);

	n = vbe32dec(f->data);
	f->md.prio.stream = n & ~(1U << 31);

	s->dependency = f->md.prio.stream;
	if (n >> 31){
		f->md.prio.exclusive = 1;
		exclusive_stream_dependency(s);
	}

	buf += 4;
	f->md.prio.weight = *buf;
	s->weight = f->md.prio.weight;

	vtc_log(hp->vl, 3, "prio->stream: %u", f->md.prio.stream);
	vtc_log(hp->vl, 3, "prio->weight: %u", f->md.prio.weight);
}

static void
parse_rst(const struct stream *s, struct frame *f)
{
	struct http *hp;
	uint32_t err;
	const char *buf;
	CHECK_OBJ_NOTNULL(f, FRAME_MAGIC);
	CHECK_OBJ_NOTNULL(s, STREAM_MAGIC);
	CAST_OBJ_NOTNULL(hp, s->hp, HTTP_MAGIC);;

	if (f->size != 4)
		vtc_fatal(hp->vl, "Size should be 4, but isn't (%d)", f->size);

	err = vbe32dec(f->data);
	f->md.rst_err = err;

	vtc_log(hp->vl, 2, "ouch");
	if (err <= ERR_MAX)
		buf = h2_errs[err];
	else
		buf = "unknown";
	vtc_log(hp->vl, 4, "rst->err: %s (%d)", buf, err);

}

static void
parse_settings(const struct stream *s, struct frame *f)
{
	struct http *hp;
	int i, t, v;
	const char *buf;
	enum hpk_result r;
	CHECK_OBJ_NOTNULL(f, FRAME_MAGIC);
	CHECK_OBJ_NOTNULL(s, STREAM_MAGIC);
	CAST_OBJ_NOTNULL(hp, s->hp, HTTP_MAGIC);;

	if (f->size % 6)
		vtc_fatal(hp->vl,
		    "Size should be a multiple of 6, but isn't (%d)", f->size);

	for (i = 0; i <= SETTINGS_MAX; i++)
		f->md.settings[i] = NAN;

	for (i = 0; i < f->size;) {
		t = vbe16dec(f->data + i);
		i += 2;
		v = vbe32dec(f->data + i);
		if (t <= SETTINGS_MAX) {
			buf = h2_settings[t];
			f->md.settings[t] = v;
		} else
			buf = "unknown";
		i += 4;

		if (t == 1) {
			r = HPK_ResizeTbl(s->hp->encctx, v);
			assert(r == hpk_done);
		}

		vtc_log(hp->vl, 4, "settings->%s (%d): %d", buf, t, v);
	}

}

static void
parse_ping(const struct stream *s, struct frame *f)
{
	struct http *hp;
	CHECK_OBJ_NOTNULL(f, FRAME_MAGIC);
	CHECK_OBJ_NOTNULL(s, STREAM_MAGIC);
	CAST_OBJ_NOTNULL(hp, s->hp, HTTP_MAGIC);;
	if (f->size != 8)
		vtc_fatal(hp->vl, "Size should be 8, but isn't (%d)", f->size);
	f->md.ping.ack = f->flags & ACK;
	memcpy(f->md.ping.data, f->data, 8);
	f->md.ping.data[8] = '\0';

	vtc_log(hp->vl, 4, "ping->data: %s", f->md.ping.data);

}

static void
parse_goaway(const struct stream *s, struct frame *f)
{
	struct http *hp;
	const char *err_buf;
	uint32_t err, stid;
	CHECK_OBJ_NOTNULL(f, FRAME_MAGIC);
	CHECK_OBJ_NOTNULL(s, STREAM_MAGIC);
	CAST_OBJ_NOTNULL(hp, s->hp, HTTP_MAGIC);;

	if (f->size < 8)
		vtc_fatal(hp->vl,
		    "Size should be at least 8, but isn't (%d)", f->size);
	if (f->data[0] & (1<<7))
		vtc_fatal(hp->vl,
		    "First bit of data is reserved and should be 0");

	stid = vbe32dec(f->data);
	err = vbe32dec(f->data + 4);
	f->md.goaway.err = err;
	f->md.goaway.stream = stid;

	if (err <= ERR_MAX)
		err_buf = h2_errs[err];
	else
		err_buf = "unknown";

	if (f->size > 8) {
		f->md.goaway.debug = malloc((f->size - 8) + 1L);
		AN(f->md.goaway.debug);
		f->md.goaway.debug[f->size - 8] = '\0';

		memcpy(f->md.goaway.debug, f->data + 8, f->size - 8);
	}

	vtc_log(hp->vl, 3, "goaway->laststream: %d", stid);
	vtc_log(hp->vl, 3, "goaway->err: %s (%d)", err_buf, err);
	if (f->md.goaway.debug)
		vtc_log(hp->vl, 3, "goaway->debug: %s", f->md.goaway.debug);
}

static void
parse_winup(const struct stream *s, struct frame *f)
{
	struct http *hp;
	uint32_t size;
	CHECK_OBJ_NOTNULL(f, FRAME_MAGIC);
	CHECK_OBJ_NOTNULL(s, STREAM_MAGIC);
	CAST_OBJ_NOTNULL(hp, s->hp, HTTP_MAGIC);;

	if (f->size != 4)
		vtc_fatal(hp->vl, "Size should be 4, but isn't (%d)", f->size);
	if (f->data[0] & (1<<7))
		vtc_log(hp->vl, s->hp->fatal,
		    "First bit of data is reserved and should be 0");

	size = vbe32dec(f->data);
	f->md.winup_size = size;

	vtc_log(hp->vl, 3, "winup->size: %d", size);
}

/* read a frame and queue it in the relevant stream, wait if not present yet.
 */
static void *
receive_frame(void *priv)
{
	struct http *hp;
	char hdr[9];
	struct frame *f;
	struct stream *s;
	int expect_cont = 0;
	struct vsb *vsb = NULL;
	struct hpk_hdr *hdrs = NULL;

	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);

	AZ(pthread_mutex_lock(&hp->mtx));
	while (hp->h2) {
		/*no wanted frames? */
		assert(hp->wf >= 0);
		if (hp->wf == 0) {
			AZ(pthread_cond_wait(&hp->cond, &hp->mtx));
			continue;
		}
		AZ(pthread_mutex_unlock(&hp->mtx));

		if (!get_bytes(hp, hdr, 9)) {
			AZ(pthread_mutex_lock(&hp->mtx));
			VTAILQ_FOREACH(s, &hp->streams, list)
				AZ(pthread_cond_signal(&s->cond));
			AZ(pthread_mutex_unlock(&hp->mtx));
			vtc_log(hp->vl, hp->fatal,
			    "could not get frame header");
			return (NULL);
		}
		ALLOC_OBJ(f, FRAME_MAGIC);
		AN(f);
		readFrameHeader(f, hdr);

		vtc_log(hp->vl, 3, "rx: stream: %d, type: %s (%d), "
				"flags: 0x%02x, size: %d",
				f->stid,
				f->type < TYPE_MAX ? h2_types[f->type] : "?",
				f->type, f->flags, f->size);
		explain_flags(f->flags, f->type, hp->vl);

		if (f->size) {
			f->data = malloc(f->size + 1L);
			AN(f->data);
			f->data[f->size] = '\0';
			if (get_bytes(hp, f->data, f->size) <= 0) {
				AZ(pthread_mutex_lock(&hp->mtx));
				VTAILQ_FOREACH(s, &hp->streams, list)
					AZ(pthread_cond_signal(&s->cond));
				clean_frame(&f);
				AZ(pthread_mutex_unlock(&hp->mtx));
				vtc_log(hp->vl, hp->fatal,
				    "could not get frame body");
				return (NULL);
			}
		}

		/* is the corresponding stream waiting? */
		AZ(pthread_mutex_lock(&hp->mtx));
		s = NULL;
		while (!s) {
			VTAILQ_FOREACH(s, &hp->streams, list)
				if (s->id == f->stid)
					break;
			if (!s)
				AZ(pthread_cond_wait(&hp->cond, &hp->mtx));
			if (!hp->h2) {
				clean_frame(&f);
				AZ(pthread_mutex_unlock(&hp->mtx));
				return (NULL);
			}
		}
		AZ(pthread_mutex_unlock(&hp->mtx));

		if (expect_cont &&
		    (f->type != TYPE_CONTINUATION || expect_cont != s->id))
			vtc_fatal(hp->vl, "Expected CONTINUATION frame for "
			    "stream %u", expect_cont);

		/* parse the frame according to it type, and fill the metada */
		switch (f->type) {
			case TYPE_DATA:
				parse_data(s, f);
				break;
			case TYPE_PUSH_PROMISE:
				hdrs = s->req;
				/*FALLTHROUGH*/
			case TYPE_HEADERS:
				if (!hdrs) {
					if (hp->sfd)
						hdrs = s->req;
					else
						hdrs = s->resp;
				}
				hdrs[0].t = 0;
				AZ(vsb);
				vsb = VSB_new_auto();
				/*FALLTHROUGH*/
			case TYPE_CONTINUATION:
				AN(hdrs);
				expect_cont = s->id;
				parse_hdr(s, f, vsb);
				if (f->flags & END_HEADERS) {
					expect_cont = 0;
					AZ(VSB_finish(vsb));
					decode_hdr(hp, hdrs, vsb);
					VSB_destroy(&vsb);
					hdrs = NULL;
				}
				break;
			case TYPE_PRIORITY:
				parse_prio(s, f);
				break;
			case TYPE_RST_STREAM:
				parse_rst(s, f);
				break;
			case TYPE_SETTINGS:
				parse_settings(s, f);
				break;
			case TYPE_PING:
				parse_ping(s, f);
				break;
			case TYPE_GOAWAY:
				parse_goaway(s, f);
				break;
			case TYPE_WINDOW_UPDATE:
				parse_winup(s, f);
				break;
			default:
				WRONG("wrong frame type");
		}

		AZ(pthread_mutex_lock(&hp->mtx));
		VTAILQ_INSERT_HEAD(&s->fq, f, list);
		if (s->wf) {
			assert(hp->wf > 0);
			hp->wf--;
			s->wf = 0;
			AZ(pthread_cond_signal(&s->cond));
		}
		continue;
	}
	AZ(pthread_mutex_unlock(&hp->mtx));
	if (!vtc_error)
		AZ(vsb);
	return (NULL);
}

#define STRTOU32(n, ss, p, v, c)					\
	do {								\
		n = strtoul(ss, &p, 0);					\
		if (*p != '\0')						\
			vtc_fatal(v, "%s takes an integer as argument"	\
				"(found %s)", c, ss);			\
	} while (0)

#define STRTOU32_CHECK(n, sp, p, v, c, l)				\
do {									\
	sp++;								\
	AN(*sp);							\
	STRTOU32(n, *sp, p, v, c);					\
	if (l && n >= (1U << l))					\
		vtc_fatal(v,						\
		    c " must be a %d-bits integer (found %s)", l, *sp);	\
} while (0)

#define CHECK_LAST_FRAME(TYPE) \
	if (!f || f->type != TYPE_ ## TYPE) {				   \
		vtc_fatal(s->hp->vl, "Last frame was not of type " #TYPE); \
	}

#define RETURN_SETTINGS(idx) \
do { \
	if (isnan(f->md.settings[idx])) { \
		return (NULL); \
	} \
	snprintf(buf, 20, "%.0f", f->md.settings[idx]); \
	return (buf); \
} while (0)

#define RETURN_BUFFED(val) \
do { \
	snprintf(buf, 20, "%d", val); \
	return (buf); \
} while (0)

static char *
find_header(const struct hpk_hdr *h, const char *k)
{
	AN(k);

	int kl = strlen(k);
	while (h->t) {
		if (kl == h->key.len  && !strncasecmp(h->key.ptr, k, kl))
			return h->value.ptr;
		h++;
	}
	return (NULL);
}
/* SECTION: stream.spec.zexpect expect
 *
 * expect in stream works as it does in client or server, except that the
 * elements compared will be different.
 *
 * Most of these elements will be frame specific, meaning that the last frame
 * received on that stream must of the correct type.
 *
 * Here the list of keywords you can look at.
 */
static const char *
cmd_var_resolve(const struct stream *s, const char *spec, char *buf)
{
	uint32_t idx;
	int n;
	const struct hpk_hdr *h;
	struct hpk_ctx *ctx;
	struct frame *f = s->frame;

	CHECK_OBJ_NOTNULL(s, STREAM_MAGIC);
	CHECK_OBJ_NOTNULL(s->hp, HTTP_MAGIC);
	AN(spec);
	AN(buf);

	n = 0;
	/* SECTION: stream.spec.zexpect.ping PING specific
	 * ping.data
	 *	The 8-bytes string of the PING frame payload.
	 * ping.ack (PING)
	 *	"true" if the ACK flag was set, "false" otherwise.
	 */
	if (!strcmp(spec, "ping.data")) {
		CHECK_LAST_FRAME(PING);
		return (f->md.ping.data);
	}
	else if (!strcmp(spec, "ping.ack")) {
		CHECK_LAST_FRAME(PING);
		snprintf(buf, 20, (f->flags & ACK) ? "true" : "false");
		return (buf);
	}
	/* SECTION: stream.spec.zexpect.winup WINDOW_UPDATE specific
	 * winup.size
	 *	The size of the upgrade given by the WINDOW_UPDATE frame.
	 */
	else if (!strcmp(spec, "winup.size")) {
		CHECK_LAST_FRAME(WINDOW_UPDATE);
		RETURN_BUFFED(f->md.winup_size);
	}
	/* SECTION: stream.spec.zexpect.prio PRIORITY specific
	 * prio.stream
	 *	The stream ID announced.
	 *
	 * prio.exclusive
	 *	"true" if the priority is exclusive, else "false".
	 *
	 * prio.weight
	 *	The dependency weight.
	 */
	else if (!strcmp(spec, "prio.stream")) {
		CHECK_LAST_FRAME(PRIORITY);
		RETURN_BUFFED(f->md.prio.stream);
	}
	else if (!strcmp(spec, "prio.exclusive")) {
		CHECK_LAST_FRAME(PRIORITY);
		snprintf(buf, 20, f->md.prio.exclusive ? "true" : "false");
		return (buf);
	}
	else if (!strcmp(spec, "prio.weight")) {
		CHECK_LAST_FRAME(PRIORITY);
		RETURN_BUFFED(f->md.prio.weight);
	}
	/* SECTION: stream.spec.zexpect.rst RESET_STREAM specific
	 * rst.err
	 *	The error code (as integer) of the RESET_STREAM frame.
	 */
	else if (!strcmp(spec, "rst.err")) {
		CHECK_LAST_FRAME(RST_STREAM);
		RETURN_BUFFED(f->md.rst_err);
	}
	/* SECTION: stream.spec.zexpect.settings SETTINGS specific
	 *
	 * settings.ack
	 *	"true" if the ACK flag was set, else ""false.
	 *
	 * settings.push
	 *	"true" if the push settings was set to yes, "false" if set to
	 *	no, and <undef> if not present.
	 *
	 * settings.hdrtbl
	 *	Value of HEADER_TABLE_SIZE if set, <undef> otherwise.
	 *
	 * settings.maxstreams
	 *	Value of MAX_CONCURRENT_STREAMS if set, <undef> otherwise.
	 *
	 * settings.winsize
	 *	Value of INITIAL_WINDOW_SIZE if set, <undef> otherwise.
	 *
	 * setting.framesize
	 *	Value of MAX_FRAME_SIZE if set, <undef> otherwise.
	 *
	 * settings.hdrsize
	 *	Value of MAX_HEADER_LIST_SIZE if set, <undef> otherwise.
	 */
	else if (!strncmp(spec, "settings.", 9)) {
		CHECK_LAST_FRAME(SETTINGS);
		spec += 9;
		if (!strcmp(spec, "ack")) {
			snprintf(buf, 20, (f->flags & ACK) ? "true" : "false");
			return (buf);
		}
		else if (!strcmp(spec, "push")) {
			if (isnan(f->md.settings[2]))
				return (NULL);
			else if (f->md.settings[2] == 1)
				snprintf(buf, 20, "true");
			else
				snprintf(buf, 20, "false");
			return (buf);
		}
		else if (!strcmp(spec, "hdrtbl"))     { RETURN_SETTINGS(1); }
		else if (!strcmp(spec, "maxstreams")) { RETURN_SETTINGS(3); }
		else if (!strcmp(spec, "winsize"))    { RETURN_SETTINGS(4); }
		else if (!strcmp(spec, "framesize"))  { RETURN_SETTINGS(5); }
		else if (!strcmp(spec, "hdrsize"))    { RETURN_SETTINGS(6); }
	}
	/* SECTION: stream.spec.zexpect.push PUSH_PROMISE specific
	 * push.id
	 *	The id of the promised stream.
	 */
	else if (!strcmp(spec, "push.id")) {
		CHECK_LAST_FRAME(PUSH_PROMISE);
		RETURN_BUFFED(f->md.promised);
	}
	/* SECTION: stream.spec.zexpect.goaway GOAWAY specific
	 * goaway.err
	 *	The error code (as integer) of the GOAWAY frame.
	 *
	 * goaway.laststream
	 *	Last-Stream-ID
	 *
	 * goaway.debug
	 *	Debug data, if any.
	 */
	else if (!strncmp(spec, "goaway.", 7)) {
		spec += 7;
		CHECK_LAST_FRAME(GOAWAY);

		if (!strcmp(spec, "err"))
			RETURN_BUFFED(f->md.goaway.err);
		else if (!strcmp(spec, "laststream"))
			RETURN_BUFFED(f->md.goaway.stream);
		else if (!strcmp(spec, "debug"))
			return (f->md.goaway.debug);
	}
	/* SECTION: stream.spec.zexpect.zframe Generic frame
	 * frame.data
	 *	Payload of the last frame
	 *
	 * frame.type
	 *	Type of the frame, as integer.
	 *
	 * frame.size
	 *	Size of the frame.
	 *
	 * frame.stream
	 *	Stream of the frame (correspond to the one you are executing
	 *	this from, obviously).
	 *
	 * frame.padding (for DATA, HEADERS, PUSH_PROMISE frames)
	 *	Number of padded bytes.
	 */
	else if (!strncmp(spec, "frame.", 6)) {
		spec += 6;
		if (!f)
			vtc_fatal(s->hp->vl, "No frame received yet.");
		if (!strcmp(spec, "data"))   { return (f->data); }
		else if (!strcmp(spec, "type"))   { RETURN_BUFFED(f->type); }
		else if (!strcmp(spec, "size"))	  { RETURN_BUFFED(f->size); }
		else if (!strcmp(spec, "stream")) { RETURN_BUFFED(f->stid); }
		else if (!strcmp(spec, "padding")) {
			if (f->type != TYPE_DATA &&
					f->type != TYPE_HEADERS &&
					f->type != TYPE_PUSH_PROMISE)
				vtc_fatal(s->hp->vl,
						"Last frame was not of type "
						"DATA, HEADERS or PUSH");
			RETURN_BUFFED(f->md.padded);
		}
	}
	/* SECTION: stream.spec.zexpect.zstream Stream
	 * stream.window
	 *	The current window size of the stream, or, if on stream 0,
	 *	of the connection.
	 *
	 * stream.weight
	 *	Weight of the stream
	 *
	 * stream.dependency
	 *	Id of the stream this one depends on.
	 */
	else if (!strcmp(spec, "stream.window")) {
		snprintf(buf, 20, "%jd",
		    (intmax_t)(s->id ? s->ws : s->hp->ws));
		return (buf);
	}
	else if (!strcmp(spec, "stream.weight")) {
		if (s->id) {
			snprintf(buf, 20, "%d", s->weight);
			return (buf);
		} else
			return (NULL);
	}
	else if (!strcmp(spec, "stream.dependency")) {
		if (s->id) {
			snprintf(buf, 20, "%d", s->dependency);
			return (buf);
		} else
			return (NULL);
	}
	/* SECTION: stream.spec.zexpect.ztable Index tables
	 * tbl.dec.size / tbl.enc.size
	 *	Size (bytes) of the decoding/encoding table.
	 *
	 * tbl.dec.size / tbl.enc.maxsize
	 *	Maximum size (bytes) of the decoding/encoding table.
	 *
	 * tbl.dec.length / tbl.enc.length
	 *	Number of headers in decoding/encoding table.
	 *
	 * tbl.dec[INT].key / tbl.enc[INT].key
	 *	Name of the header at index INT of the decoding/encoding
	 *	table.
	 *
	 * tbl.dec[INT].value / tbl.enc[INT].value
	 *	Value of the header at index INT of the decoding/encoding
	 *	table.
	 */
	else if (!strncmp(spec, "tbl.dec", 7) || !strncmp(spec, "tbl.enc", 7)) {
		if (spec[4] == 'd')
			ctx = s->hp->decctx;
		else
			ctx = s->hp->encctx;
		spec += 7;

		if (1 == sscanf(spec, "[%u].key%n", &idx, &n) &&
				spec[n] == '\0') {
			h = HPK_GetHdr(ctx, idx + 61);
			return (h ? h->key.ptr : NULL);
		}
		else if (1 == sscanf(spec, "[%u].value%n", &idx, &n) &&
				spec[n] == '\0') {
			h = HPK_GetHdr(ctx, idx + 61);
			return (h ? h->value.ptr : NULL);
		}
		else if (!strcmp(spec, ".size"))
			RETURN_BUFFED(HPK_GetTblSize(ctx));
		else if (!strcmp(spec, ".maxsize"))
			RETURN_BUFFED(HPK_GetTblMaxSize(ctx));
		else if (!strcmp(spec, ".length"))
			RETURN_BUFFED(HPK_GetTblLength(ctx));
	}
	/* SECTION: stream.spec.zexpect.zre Request and response
	 *
	 * Note: it's possible to inspect a request or response while it is
	 * still being construct (in-between two frames for example).
	 *
	 * req.bodylen / resp.bodylen
	 *	Length in bytes of the request/response so far.
	 *
	 * req.body / resp.body
	 *	Body of the request/response so far.
	 *
	 * req.http.STRING / resp.http.STRING
	 *	Value of the header STRING in the request/response.
	 *
	 * req.status / resp.status
	 *	:status pseudo-header's value.
	 *
	 * req.url / resp.url
	 *	:path pseudo-header's value.
	 *
	 * req.method / resp.method
	 *	:method pseudo-header's value.
	 *
	 * req.authority / resp.authority
	 *	:method pseudo-header's value.
	 *
	 * req.scheme / resp.scheme
	 *	:method pseudo-header's value.
	 */
	else if (!strncmp(spec, "req.", 4) || !strncmp(spec, "resp.", 5)) {
		if (spec[2] == 'q') {
			h = s->req;
			spec += 4;
		} else {
			h = s->resp;
			spec += 5;
		}
		if (!strcmp(spec, "body"))
			return (s->body);
		else if (!strcmp(spec, "bodylen"))
			RETURN_BUFFED(s->bodylen);
		else if (!strcmp(spec, "status"))
			return (find_header(h, ":status"));
		else if (!strcmp(spec, "url"))
			return (find_header(h, ":path"));
		else if (!strcmp(spec, "method"))
			return (find_header(h, ":method"));
		else if (!strcmp(spec, "authority"))
			return (find_header(h, ":authority"));
		else if (!strcmp(spec, "scheme"))
			return (find_header(h, ":scheme"));
		else if (!strncmp(spec, "http.", 5))
			return (find_header(h, spec + 5));
		else
			return (NULL);
	}
#define H2_ERROR(U,v,sc,t) \
	else if (!strcmp(spec, #U)) { return (#v); }
#include "tbl/h2_error.h"
	else
		return (spec);
	return (NULL);
}

/* SECTION: stream.spec.frame_sendhex sendhex
 *
 * Push bytes directly on the wire. sendhex takes exactly one argument: a string
 * describing the bytes, in hex notation, will possible whitespaces between
 * them. Here's an example::
 *
 *	sendhex "00 00 08 00 0900	8d"
 */
static void
cmd_sendhex(CMD_ARGS)
{
	struct http *hp;
	struct stream *s;
	struct vsb *vsb;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);
	CAST_OBJ_NOTNULL(hp, s->hp, HTTP_MAGIC);
	AN(av[1]);
	AZ(av[2]);
	vsb = vtc_hex_to_bin(hp->vl, av[1]);
	assert(VSB_len(vsb) >= 0);
	vtc_hexdump(hp->vl, 4, "sendhex", VSB_data(vsb), VSB_len(vsb));
	AZ(pthread_mutex_lock(&hp->mtx));
	http_write(hp, 4, VSB_data(vsb), VSB_len(vsb), "sendhex");
	AZ(pthread_mutex_unlock(&hp->mtx));
	VSB_destroy(&vsb);
}

#define ENC(hdr, k, v)			\
{					\
	AN(k);				\
	hdr.key.ptr = strdup(k);	\
	AN(hdr.key.ptr);		\
	hdr.key.len = strlen(k);	\
	AN(v);				\
	hdr.value.ptr = strdup(v);	\
	AN(hdr.value.ptr);		\
	hdr.value.len = strlen(v);	\
	(void)HPK_EncHdr(iter, &hdr);	\
	free(hdr.key.ptr);		\
	free(hdr.value.ptr);		\
}

#define STR_ENC(av, field, str)						       \
{									       \
	av++;								       \
	     if (AV_IS("plain")) { hdr.field.huff = 0; }		       \
	else if (AV_IS("huf"))   { hdr.field.huff = 1; }		       \
	else								       \
		vtc_fatal(vl, str " arg can be huf or plain (got: %s)", *av); \
	av++;								       \
	AN(*av);							       \
	hdr.field.ptr = *av;						       \
	hdr.field.len = strlen(*av);					       \
}


/* SECTION: stream.spec.data_0 txreq, txresp, txcont, txpush
 *
 * These four commands are about sending headers. txreq and txresp
 * will send HEADER frames; txcont will send CONTINUATION frames; txpush
 * PUSH frames.
 * The only difference between txreq and txresp are the default headers
 * set by each of them.
 *
 * \-noadd
 *	Do not add default headers. Useful to avoid duplicates when sending
 *	default headers using ``-hdr``, ``-idxHdr`` and ``-litIdxHdr``.
 *
 * \-status INT (txresp)
 *	Set the :status pseudo-header.
 *
 * \-url STRING (txreq, txpush)
 *	Set the :path pseudo-header.
 *
 * \-req STRING (txreq, txpush)
 *	Set the :method pseudo-header.
 *
 * \-scheme STRING (txreq, txpush)
 *	Set the :scheme pseudo-header.
 *
 * \-hdr STRING1 STRING2
 *	Insert a header, STRING1 being the name, and STRING2 the value.
 *
 * \-idxHdr INT
 *	Insert an indexed header, using INT as index.
 *
 * \-litIdxHdr inc|not|never INT huf|plain STRING
 *	Insert an literal, indexed header. The first argument specify if the
 *	header should be added to the table, shouldn't, or mustn't be
 *	compressed if/when retransmitted.
 *
 *	INT is the idex of the header name to use.
 *
 *	The third argument informs about the Huffman encoding: yes (huf) or
 *	no (plain).
 *
 *	The last term is the literal value of the header.
 *
 * \-litHdr inc|not|never huf|plain STRING1 huf|plain STRING2
 *	Insert a literal header, with the same first argument as
 *	``-litIdxHdr``.
 *
 *	The second and third terms tell what the name of the header is and if
 *	it should be Huffman-encoded, while the last two do the same
 *	regarding the value.
 *
 * \-body STRING (txreq, txresp)
 *	Specify a body, effectively putting STRING into a DATA frame after
 *	the HEADER frame is sent.
 *
 * \-bodylen INT (txreq, txresp)
 *	Do the same thing as ``-body`` but generate an string of INT length
 *	for you.
 *
 * \-nostrend (txreq, txresp)
 *	Don't set the END_STREAM flag automatically, making the peer expect
 *	a body after the headers.
 *
 * \-nohdrend
 *	Don't set the END_HEADERS flag automatically, making the peer expect
 *	more HEADER frames.
 *
 * \-dep INT (txreq, txresp)
 *	Tell the peer that this content depends on the stream with the INT
 *	id.
 *
 * \-ex (txreq, txresp)
 *	Make the dependency exclusive (``-dep`` is still needed).
 *
 * \-weight (txreq, txresp)
 *	Set the weight for the dependency.
 *
 * \-promised INT (txpush)
 *	The id of the promised stream.
 *
 * \-pad STRING / -padlen INT (txreq, txresp, txpush)
 *	Add string as padding to the frame, either the one you provided with
 *	\-pad, or one that is generated for you, of length INT is -padlen
 *	case.
 */

#define cmd_txreq	cmd_tx11obj
#define cmd_txresp	cmd_tx11obj
#define cmd_txpush	cmd_tx11obj
#define cmd_txcont	cmd_tx11obj

static void
cmd_tx11obj(CMD_ARGS)
{
	struct stream *s;
	int status_done = 1;
	int method_done = 1;
	int path_done = 1;
	int scheme_done = 1;
	uint32_t stid = 0, pstid;
	uint32_t weight = 16;
	int exclusive = 0;
	char *buf;
	struct hpk_iter *iter;
	struct frame f;
	char *body = NULL, *pad = NULL;
	/*XXX: do we need a better api? yes we do */
	struct hpk_hdr hdr;
	char *cmd_str = *av;
	char *p;
	(void)cmd;

	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);
	INIT_FRAME(f, CONTINUATION, 0, s->id, END_HEADERS);
	buf = malloc(BUF_SIZE);
	AN(buf);

	if (!strcmp(cmd_str, "txreq")) {
		ONLY_CLIENT(s->hp, av);
		f.type = TYPE_HEADERS;
		f.flags |= END_STREAM;
		method_done = 0;
		path_done = 0;
		scheme_done = 0;
	} else if (!strcmp(cmd_str, "txresp")) {
		ONLY_SERVER(s->hp, av);
		f.type = TYPE_HEADERS;
		f.flags |= END_STREAM;
		status_done = 0;
	} else if (!strcmp(cmd_str, "txpush")) {
		ONLY_SERVER(s->hp, av);
		f.type = TYPE_PUSH_PROMISE;
		method_done = 0;
		path_done = 0;
		scheme_done = 0;
	}

	if (f.type == TYPE_PUSH_PROMISE) {
		*buf = 0;
		iter = HPK_NewIter(s->hp->encctx, buf + 4, BUF_SIZE - 4);
	} else
		iter = HPK_NewIter(s->hp->encctx, buf, BUF_SIZE);

#define AV_IS(str) !strcmp(*av, str)
#define CMD_IS(str) !strcmp(cmd_str, str)
	while (*++av) {
		memset(&hdr, 0, sizeof(hdr));
		hdr.t = hpk_not;
		if (AV_IS("-noadd")) {
			path_done = 1;
			status_done = 1;
			method_done = 1;
			scheme_done = 1;
		}
		else if (AV_IS("-status") && CMD_IS("txresp")) {
			ENC(hdr, ":status", av[1]);
			av++;
			status_done = 1;
		}
		else if (AV_IS("-url") &&
				(CMD_IS("txreq") || CMD_IS("txpush"))) {
			ENC(hdr, ":path", av[1]);
			av++;
			path_done = 1;
		}
		else if (AV_IS("-req") &&
				(CMD_IS("txreq") || CMD_IS("txpush"))) {
			ENC(hdr, ":method", av[1]);
			av++;
			method_done = 1;
		}
		else if (AV_IS("-scheme") &&
				(CMD_IS("txreq") || CMD_IS("txpush"))) {
			ENC(hdr, ":scheme", av[1]);
			av++;
			scheme_done = 1;
		}
		else if (AV_IS("-hdr")) {
			if (av[2] == NULL)
				vtc_fatal(vl, "-hdr takes two arguments in http2");
			ENC(hdr, av[1], av[2]);
			av += 2;
		}
		else if (AV_IS("-idxHdr")) {
			hdr.t = hpk_idx;
			STRTOU32_CHECK(hdr.i, av, p, vl, "-idxHdr", 0);
			HPK_EncHdr(iter, &hdr);
		}
		else if (AV_IS("-litIdxHdr")) {
			av++;
			if      (AV_IS("inc"))   { hdr.t = hpk_inc;   }
			else if (AV_IS("not"))   { hdr.t = hpk_not;   }
			else if (AV_IS("never")) { hdr.t = hpk_never; }
			else
				vtc_fatal(vl, "first -litidxHdr arg can be "
				    "inc, not, never (got: %s)", *av);

			STRTOU32_CHECK(hdr.i, av, p, vl,
			    "second -litidxHdr arg", 0);

			hdr.key.ptr = NULL;
			hdr.key.len = 0;
			STR_ENC(av, value,   "third -litHdr");
			HPK_EncHdr(iter, &hdr);
		}
		else if (AV_IS("-litHdr")) {
			av++;
			if      (AV_IS("inc"))   { hdr.t = hpk_inc;   }
			else if (AV_IS("not"))   { hdr.t = hpk_not;   }
			else if (AV_IS("never")) { hdr.t = hpk_never; }
			else
				vtc_fatal(vl, "first -litHdr arg can be inc, "
				    "not, never (got: %s)", *av);

			STR_ENC(av, key,   "second -litHdr");
			STR_ENC(av, value, "fourth -litHdr");
			HPK_EncHdr(iter, &hdr);
		}
		else if (AV_IS("-nostrend")) {
			f.flags &= ~END_STREAM;
		}
		else if (AV_IS("-nohdrend")) {
			f.flags &= ~END_HEADERS;
		}
		else if (AV_IS("-promised") && CMD_IS("txpush")) {
			STRTOU32_CHECK(pstid, av, p, vl, "-promised", 31);
			vbe32enc(buf, pstid);
		}
		else if (AV_IS("-pad") && !CMD_IS("txcont")) {
			AZ(pad);
			av++;
			AN(*av);
			pad = strdup(*av);
		}
		else if (AV_IS("-padlen") && !CMD_IS("txcont")) {
			AZ(pad);
			av++;
			pad = synth_body(*av, 0);
		}
		else if (CMD_IS("txreq") || CMD_IS("txresp")) {
			if (AV_IS("-body")) {
				AZ(body);
				REPLACE(body, av[1]);
				f.flags &= ~END_STREAM;
				av++;
			}
			else if (AV_IS("-bodylen")) {
				AZ(body);
				body = synth_body(av[1], 0);
				f.flags &= ~END_STREAM;
				av++;
			}else if (AV_IS("-dep")) {
				STRTOU32_CHECK(stid, av, p, vl, "-dep", 0);
				f.flags |= PRIORITY;
			}
			else if (AV_IS("-ex")) {
				exclusive = 1U << 31;
				f.flags |= PRIORITY;
			}
			else if (AV_IS("-weight")) {
				STRTOU32_CHECK(weight, av, p, vl, "-weight", 8);
				f.flags |= PRIORITY;
			} else
				break;
		} else
			break;
	}
#undef CMD_IS
#undef AV_IS
	if (*av != NULL)
		vtc_fatal(vl, "Unknown %s spec: %s\n", cmd_str, *av);

	memset(&hdr, 0, sizeof(hdr));
	hdr.t = hpk_not;

	if (!status_done) { ENC(hdr, ":status", "200"); }
	if (!path_done)   { ENC(hdr, ":path",	"/"); }
	if (!method_done) { ENC(hdr, ":method", "GET"); }
	if (!scheme_done) { ENC(hdr, ":scheme", "http"); }

	f.size = gethpk_iterLen(iter);
	if (f.flags & PRIORITY) {
		s->weight = weight & 0xff;
		s->dependency = stid;

		assert(f.size + 5 < BUF_SIZE);
		memmove(buf + 5, buf, f.size);
		vbe32enc(buf, (stid | exclusive));
		buf[4] = s->weight;
		f.size += 5;

		vtc_log(vl, 4, "stream->dependency: %u", s->dependency);
		vtc_log(vl, 4, "stream->weight: %u", s->weight);
		if (exclusive)
			exclusive_stream_dependency(s);
	}
	if (pad) {
		if (strlen(pad) >= 128)
			vtc_fatal(vl, "Padding is limited to 128 bytes");
		f.flags |= PADDED;
		assert(f.size + strlen(pad) < BUF_SIZE);
		memmove(buf + 1, buf, f.size);
		buf[0] = strlen(pad);
		f.size += 1;
		memcpy(buf + f.size, pad, strlen(pad));
		f.size += strlen(pad);
		free(pad);
	}
	if (f.type == TYPE_PUSH_PROMISE)
		f.size += 4;
	f.data = buf;
	HPK_FreeIter(iter);
	write_frame(s->hp, &f, 1);
	free(buf);

	if (!body)
		return;

	INIT_FRAME(f, DATA, strlen(body), s->id, END_STREAM);
	f.data = body;

	write_frame(s->hp, &f, 1);
	free(body);
}

/* SECTION: stream.spec.data_1 txdata
 *
 * By default, data frames are empty. The receiving end will know the whole body
 * has been delivered thanks to the END_STREAM flag set in the last DATA frame,
 * and txdata automatically set it.
 *
 * \-data STRING
 *	Data to be embedded into the frame.
 *
 * \-datalen INT
 *	Generate and INT-bytes long string to be sent in the frame.
 *
 * \-pad STRING / -padlen INT
 *	Add string as padding to the frame, either the one you provided with
 *	\-pad, or one that is generated for you, of length INT is -padlen
 *	case.
 *
 * \-nostrend
 *	Don't set the END_STREAM flag, allowing to send more data on this
 *	stream.
 */
static void
cmd_txdata(CMD_ARGS)
{
	struct stream *s;
	char *pad = NULL;
	struct frame f;
	char *body = NULL;
	char *data = NULL;
	(void)cmd;

	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);

	INIT_FRAME(f, DATA, 0, s->id, END_STREAM);

	while (*++av) {
		if (!strcmp(*av, "-data")) {
			AZ(body);
			av++;
			body = strdup(*av);
		} else if (!strcmp(*av, "-datalen")) {
			AZ(body);
			av++;
			body = synth_body(*av, 0);
		} else if (!strcmp(*av, "-pad")) {
			AZ(pad);
			av++;
			AN(*av);
			pad = strdup(*av);
		} else if (!strcmp(*av, "-padlen")) {
			AZ(pad);
			av++;
			pad = synth_body(*av, 0);
		} else if (!strcmp(*av, "-nostrend"))
			f.flags &= ~END_STREAM;
		else
			break;
	}
	if (*av != NULL)
		vtc_fatal(vl, "Unknown txdata spec: %s\n", *av);

	if (!body)
		body = strdup("");

	if (pad) {
		f.flags |= PADDED;
		if (strlen(pad) >= 128)
			vtc_fatal(vl, "Padding is limited to 128 bytes");
		data = malloc( 1 + strlen(body) + strlen(pad));
		AN(data);
		*((uint8_t *)data) = strlen(pad);
		f.size = 1;
		memcpy(data + f.size, body, strlen(body));
		f.size += strlen(body);
		memcpy(data + f.size, pad, strlen(pad));
		f.size += strlen(pad);
		f.data = data;
	} else {
		f.size = strlen(body);
		f.data = body;
	}
	write_frame(s->hp, &f, 1);
	free(body);
	free(pad);
	free(data);
}

/* SECTION: stream.spec.reset_txrst txrst
 *
 * Send a RST_STREAM frame. By default, txrst will send a 0 error code
 * (NO_ERROR).
 *
 * \-err STRING|INT
 *	Sets the error code to be sent. The argument can be an integer or a
 *	string describing the error, such as NO_ERROR, or CANCEL (see
 *	rfc7540#11.4 for more strings).
 */
static void
cmd_txrst(CMD_ARGS)
{
	struct stream *s;
	char *p;
	uint32_t err = 0;
	struct frame f;
	(void)cmd;
	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);

	INIT_FRAME(f, RST_STREAM, 4, s->id, 0);

	while (*++av) {
		if (!strcmp(*av, "-err")) {
			++av;
			for (err = 0; h2_errs[err]; err++) {
				if (!strcmp(h2_errs[err], *av))
					break;
			}

			if (h2_errs[err])
				continue;

			STRTOU32(err, *av, p, vl, "-err");
		} else
			break;
	}
	if (*av != NULL)
		vtc_fatal(vl, "Unknown txrst spec: %s\n", *av);

	err = htonl(err);
	f.data = (void *)&err;
	write_frame(s->hp, &f, 1);
}

/* SECTION: stream.spec.prio_txprio txprio
 *
 * Send a PRIORITY frame
 *
 * \-stream INT
 *	indicate the id of the stream the sender stream depends on.
 *
 * \-ex
 *	the dependency should be made exclusive (only this streams depends on
 *	the parent stream).
 *
 * \-weight INT
 *	an 8-bits integer is used to balance priority between streams
 *	depending on the same streams.
 */
static void
cmd_txprio(CMD_ARGS)
{
	struct stream *s;
	char *p;
	uint32_t stid = 0;
	struct frame f;
	uint32_t weight = 0;
	uint32_t exclusive = 0;
	uint8_t buf[5];

	(void)cmd;
	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);

	INIT_FRAME(f, PRIORITY, 5, s->id, 0);
	f.data = (void *)buf;

	while (*++av) {
		if (!strcmp(*av, "-stream")) {
			STRTOU32_CHECK(stid, av, p, vl, "-stream", 0);
		} else if (!strcmp(*av, "-ex")) {
			exclusive = 1U << 31;
		} else if (!strcmp(*av, "-weight")) {
			STRTOU32_CHECK(weight, av, p, vl, "-weight", 8);
		} else
			break;
	}
	if (*av != NULL)
		vtc_fatal(vl, "Unknown txprio spec: %s\n", *av);
	s->weight = weight & 0xff;
	s->dependency = stid;

	if (exclusive)
		exclusive_stream_dependency(s);

	vbe32enc(buf, (stid | exclusive));
	buf[4] = s->weight;
	write_frame(s->hp, &f, 1);
}

#define PUT_KV(av, vl, name, val, code) \
	do {\
		STRTOU32_CHECK(val, av, p, vl, #name, 0);	\
		vbe16enc(cursor, code);				\
		cursor += sizeof(uint16_t);			\
		vbe32enc(cursor, val);				\
		cursor += sizeof(uint32_t);			\
		f.size += 6;					\
	} while(0)

/* SECTION: stream.spec.settings_txsettings txsettings
 *
 * SETTINGS frames must be acknowledge, arguments are as follow (most of them
 * are from  rfc7540#6.5.2):
 *
 * \-hdrtbl INT
 *	headers table size
 *
 * \-push BOOL
 *	whether push frames are accepted or not
 *
 * \-maxstreams INT
 *	maximum concurrent streams allowed
 *
 * \-winsize INT
 *	sender's initial window size
 *
 * \-framesize INT
 *	largest frame size authorized
 *
 * \-hdrsize INT
 *	maximum size of the header list authorized
 *
 * \-ack
 *	set the ack bit
 */
static void
cmd_txsettings(CMD_ARGS)
{
	struct stream *s, *_s;
	struct http *hp;
	char *p;
	uint32_t val = 0;
	struct frame f;
	//TODO dynamic alloc
	char buf[512];
	char *cursor = buf;

	(void)cmd;
	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);
	CAST_OBJ_NOTNULL(hp, s->hp, HTTP_MAGIC);

	memset(buf, 0, 512);
	INIT_FRAME(f, SETTINGS, 0, s->id, 0);
	f.data = buf;

	AZ(pthread_mutex_lock(&hp->mtx));
	while (*++av) {
		if (!strcmp(*av, "-push")) {
			++av;
			vbe16enc(cursor, 0x2);
			cursor += sizeof(uint16_t);
			if (!strcmp(*av, "false"))
				vbe32enc(cursor, 0);
			else if (!strcmp(*av, "true"))
				vbe32enc(cursor, 1);
			else
				vtc_fatal(vl, "Push parameter is either "
				    "\"true\" or \"false\", not %s", *av);
			cursor += sizeof(uint32_t);
			f.size += 6;
		}
		else if (!strcmp(*av, "-hdrtbl")) {
			PUT_KV(av, vl, hdrtbl, val, 0x1);
			HPK_ResizeTbl(s->hp->decctx, val);
		}
		else if (!strcmp(*av, "-maxstreams"))
			PUT_KV(av, vl, maxstreams, val, 0x3);
		else if (!strcmp(*av, "-winsize"))	{
			PUT_KV(av, vl, winsize, val, 0x4);
			VTAILQ_FOREACH(_s, &hp->streams, list)
				_s->ws += (val - hp->iws);
			hp->iws = val;
		}
		else if (!strcmp(*av, "-framesize"))
			PUT_KV(av, vl, framesize, val, 0x5);
		else if (!strcmp(*av, "-hdrsize"))
			PUT_KV(av, vl, hdrsize, val, 0x6);
		else if (!strcmp(*av, "-ack"))
			f.flags |= 1;
		else
			break;
	}
	if (*av != NULL)
		vtc_fatal(vl, "Unknown txsettings spec: %s\n", *av);

	write_frame(hp, &f, 0);
	AZ(pthread_mutex_unlock(&hp->mtx));
}

/* SECTION: stream.spec.ping_txping txping
 *
 * Send PING frame.
 *
 * \-data STRING
 *	specify the payload of the frame, with STRING being an 8-char string.
 *
 * \-ack
 *	set the ACK flag.
 */
static void
cmd_txping(CMD_ARGS)
{
	struct stream *s;
	struct frame f;
	char buf[8];

	(void)cmd;
	memset(buf, 0, 8);
	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);
	INIT_FRAME(f, PING, 8, s->id, 0);

	while (*++av) {
		if (!strcmp(*av, "-data")) {
			av++;
			if (f.data)
				vtc_fatal(vl, "this frame already has data");
			if (strlen(*av) != 8)
				vtc_fatal(vl, "data must be a 8-char string, found  (%s)", *av);
			f.data = *av;
		} else if (!strcmp(*av, "-ack"))
			f.flags |= 1;
		else
			break;
	}
	if (*av != NULL)
		vtc_fatal(vl, "Unknown txping spec: %s\n", *av);
	if (!f.data)
		f.data = buf;
	write_frame(s->hp, &f, 1);
}

/*
 * SECTION: stream.spec.goaway_txgoaway txgoaway
 *
 * Possible options include:
 *
 * \-err STRING|INT
 *	set the error code to explain the termination. The second argument
 *	can be a integer or the string version of the error code as found
 *	in rfc7540#7.
 *
 * \-laststream INT
 *	the id of the "highest-numbered stream identifier for which the
 *	sender of the GOAWAY frame might have taken some action on or might
 *	yet take action on".
 *
 * \-debug
 *	specify the debug data, if any to append to the frame.
 */
static void
cmd_txgoaway(CMD_ARGS)
{
	struct stream *s;
	char *p;
	uint32_t err = 0;
	uint32_t ls = 0;
	struct frame f;

	(void)cmd;
	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);

	INIT_FRAME(f, GOAWAY, 8, s->id, 0);

	while (*++av) {
		if (!strcmp(*av, "-err")) {
			++av;
			for (err = 0; h2_errs[err]; err++)
				if (!strcmp(h2_errs[err], *av))
					break;

			if (h2_errs[err])
				continue;

			STRTOU32(err, *av, p, vl, "-err");
		} else if (!strcmp(*av, "-laststream")) {
			STRTOU32_CHECK(ls, av, p, vl, "-laststream", 31);
		} else if (!strcmp(*av, "-debug")) {
			++av;
			if (f.data)
				vtc_fatal(vl, "this frame already has debug data");
			f.size = 8 + strlen(*av);
			f.data = malloc(f.size);
			AN(f.data);
			memcpy(f.data + 8, *av, f.size - 8);
		} else
			break;
	}
	if (*av != NULL)
		vtc_fatal(vl, "Unknown txgoaway spec: %s\n", *av);

	if (!f.data) {
		f.data = malloc(8);
		AN(f.data);
	}
	vbe32enc(f.data, ls);
	vbe32enc(f.data + 4, err);
	write_frame(s->hp, &f, 1);
	free(f.data);
}

/* SECTION: stream.spec.winup_txwinup txwinup
 *
 * Transmit a WINDOW_UPDATE frame, increasing the amount of credit of the
 * connection (from stream 0) or of the stream (any other stream).
 *
 * \-size INT
 *	give INT credits to the peer.
 */
static void
cmd_txwinup(CMD_ARGS)
{
	struct http *hp;
	struct stream *s;
	char *p;
	struct frame f;
	char buf[8];
	uint32_t size = 0;

	(void)cmd;
	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);
	CAST_OBJ_NOTNULL(hp, s->hp, HTTP_MAGIC);
	memset(buf, 0, 8);

	AN(av[1]);
	AN(av[2]);

	INIT_FRAME(f, WINDOW_UPDATE, 4, s->id, 0);
	f.data = buf;

	while (*++av)
		if (!strcmp(*av, "-size")) {
			STRTOU32_CHECK(size, av, p, vl, "-size", 0);
		} else
			break;
	if (*av != NULL)
		vtc_fatal(vl, "Unknown txwinup spec: %s\n", *av);

	AZ(pthread_mutex_lock(&hp->mtx));
	if (s->id == 0)
		hp->ws += size;
	s->ws += size;
	AZ(pthread_mutex_unlock(&hp->mtx));

	size = htonl(size);
	f.data = (void *)&size;
	write_frame(hp, &f, 1);
}

static struct frame *
rxstuff(struct stream *s)
{
	struct frame *f;

	CHECK_OBJ_NOTNULL(s, STREAM_MAGIC);

	AZ(pthread_mutex_lock(&s->hp->mtx));
	if (VTAILQ_EMPTY(&s->fq)) {
		assert(s->hp->wf >= 0);
		s->hp->wf++;
		s->wf = 1;
		AZ(pthread_cond_signal(&s->hp->cond));
		AZ(pthread_cond_wait(&s->cond, &s->hp->mtx));
	}
	if (VTAILQ_EMPTY(&s->fq)) {
		AZ(pthread_mutex_unlock(&s->hp->mtx));
		return (NULL);
	}
	clean_frame(&s->frame);
	f = VTAILQ_LAST(&s->fq, fq_head);
	VTAILQ_REMOVE(&s->fq, f, list);
	AZ(pthread_mutex_unlock(&s->hp->mtx));

	CHECK_OBJ_NOTNULL(f, FRAME_MAGIC);
	return (f);
}

#define CHKFRAME(rt, wt, rcv, func) \
	do { \
	if (rt != wt) \
		vtc_fatal(vl, "Frame #%d for %s was of type %s (%d) " \
		    "instead of %s (%d)", \
		    rcv, func, \
		    rt < TYPE_MAX ? h2_types[rt] : "?", rt, \
		    wt < TYPE_MAX ? h2_types[wt] : "?", wt); \
	} while (0);

/* SECTION: stream.spec.data_11 rxhdrs
 *
 * ``rxhdrs`` will expect one HEADER frame, then, depending on the arguments,
 * zero or more CONTINUATION frame.
 *
 * \-all
 *	Keep waiting for CONTINUATION frames until END_HEADERS flag is seen.
 *
 * \-some INT
 *	Retrieve INT - 1 CONTINUATION frames after the HEADER frame.
 *
 */
static void
cmd_rxhdrs(CMD_ARGS)
{
	struct stream *s;
	struct frame *f = NULL;
	char *p;
	int loop = 0;
	unsigned long int times = 1;
	int rcv = 0;
	enum h2_type expect = TYPE_HEADERS;

	(void)cmd;
	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);

	while (*++av) {
		if (!strcmp(*av, "-some")) {
			STRTOU32_CHECK(times, av, p, vl, "-some", 0);
			if (!times)
				vtc_fatal(vl, "-some argument must be more"
					       "than 0 (found \"%s\")\n", *av);
		} else if (!strcmp(*av, "-all"))
			loop = 1;
		else
			break;
	}
	if (*av != NULL)
		vtc_fatal(vl, "Unknown rxhdrs spec: %s\n", *av);

	do {
		f = rxstuff(s);
		if (!f)
			return;
		rcv++;
		CHKFRAME(f->type, expect, rcv, "rxhdrs");
		expect = TYPE_CONTINUATION;
	} while (rcv < times || (loop && !(f->flags & END_HEADERS)));
	s->frame = f;
}

static void
cmd_rxcont(CMD_ARGS)
{
	struct stream *s;
	struct frame *f = NULL;
	char *p;
	int loop = 0;
	unsigned long int times = 1;
	int rcv = 0;

	(void)cmd;
	(void)av;
	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);

	while (*++av)
		if (!strcmp(*av, "-some")) {
			STRTOU32(times, *av, p, vl, "-some");
			if (!times)
				vtc_fatal(vl, "-some argument must be more"
					       "than 0 (found \"%s\")\n", *av);
		} else if (!strcmp(*av, "-all"))
			loop = 1;
		else
			break;
	if (*av != NULL)
		vtc_fatal(vl, "Unknown rxcont spec: %s\n", *av);

	do {
		f = rxstuff(s);
		if (!f)
			return;
		rcv++;
		CHKFRAME(f->type, TYPE_CONTINUATION, rcv, "rxcont");
	} while (rcv < times || (loop && !(f->flags & END_HEADERS)));
	s->frame = f;
}


/* SECTION: stream.spec.data_13 rxdata
 *
 * Receiving data is done using the ``rxdata`` keywords and will retrieve one
 * DATA frame, if you wish to receive more, you can use these two convenience
 * arguments:
 *
 * \-all
 *	keep waiting for DATA frame until one sets the END_STREAM flag
 *
 * \-some INT
 *	retrieve INT DATA frames.
 *
 */
static void
cmd_rxdata(CMD_ARGS)
{
	struct stream *s;
	struct frame *f = NULL;
	char *p;
	int loop = 0;
	unsigned long int times = 1;
	int rcv = 0;

	(void)cmd;
	(void)av;
	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);

	while (*++av)
		if (!strcmp(*av, "-some")) {
			STRTOU32(times, *av, p, vl, "-some");
			if (!times)
				vtc_fatal(vl, "-some argument must be more"
					       "than 0 (found \"%s\")\n", *av);
		} else if (!strcmp(*av, "-all"))
			loop = 1;
		else
			break;
	if (*av != NULL)
		vtc_fatal(vl, "Unknown rxdata spec: %s\n", *av);

	do {
		f = rxstuff(s);
		if (!f)
			return;
		rcv++;
		CHKFRAME(f->type, TYPE_DATA, rcv, "rxhdata");
	} while (rcv < times || (loop && !(f->flags & END_STREAM)));
	s->frame = f;
}

/* SECTION: stream.spec.data_10 rxreq, rxresp
 *
 * These are two convenience functions to receive headers and body of an
 * incoming request or response. The only difference is that rxreq can only be
 * by a server, and rxresp by a client.
 *
 */

#define cmd_rxreq	cmd_rxmsg
#define cmd_rxresp	cmd_rxmsg

static void
cmd_rxmsg(CMD_ARGS)
{
	struct stream *s;
	struct frame *f;
	int end_stream;
	int rcv = 0;

	(void)cmd;
	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);

	if (!strcmp(av[0], "rxreq"))
		ONLY_SERVER(s->hp, av);
	else
		ONLY_CLIENT(s->hp, av);

	f = rxstuff(s);
	if (!f)
		return;

	rcv++;
	CHKFRAME(f->type, TYPE_HEADERS, rcv, *av);

	end_stream = f->flags & END_STREAM;

	while (!(f->flags & END_HEADERS)) {
		f = rxstuff(s);
		if (!f)
			return;
		rcv++;
		CHKFRAME(f->type, TYPE_CONTINUATION, rcv, *av);
	}

	while (!end_stream && (f = rxstuff(s))) {
		rcv++;
		CHKFRAME(f->type, TYPE_DATA, rcv, *av);
		end_stream = f->flags & END_STREAM;
	}
	s->frame = f;
}

/* SECTION: stream.spec.data_12 rxpush
 *
 * This works like ``rxhdrs``, expecting a PUSH frame and then zero or more
 * CONTINUATION frames.
 *
 * \-all
 *	Keep waiting for CONTINUATION frames until END_HEADERS flag is seen.
 *
 * \-some INT
 *	Retrieve INT - 1 CONTINUATION frames after the PUSH frame.
 *
 */
static void
cmd_rxpush(CMD_ARGS)
{
	struct stream *s;
	struct frame *f = NULL;
	char *p;
	int loop = 0;
	unsigned long int times = 1;
	int rcv = 0;
	enum h2_type expect = TYPE_PUSH_PROMISE;

	(void)cmd;
	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);

	while (*++av) {
		if (!strcmp(*av, "-some")) {
			STRTOU32_CHECK(times, av, p, vl, "-some", 0);
			if (!times)
				vtc_fatal(vl, "-some argument must be more"
					       "than 0 (found \"%s\")\n", *av);
		} else if (!strcmp(*av, "-all")) {
			loop = 1;
		} else
			break;
	}
	if (*av != NULL)
		vtc_fatal(vl, "Unknown rxpush spec: %s\n", *av);

	do {
		f = rxstuff(s);
		if (!f)
			return;
		rcv++;
		CHKFRAME(f->type, expect, rcv, "rxpush");
		expect = TYPE_CONTINUATION;
	} while (rcv < times || (loop && !(f->flags & END_HEADERS)));
	s->frame = f;
}

#define RXFUNC(lctype, upctype) \
	static void \
	cmd_rx ## lctype(CMD_ARGS) { \
		struct stream *s; \
		(void)cmd; \
		(void)av; \
		CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC); \
		s->frame = rxstuff(s); \
		if (s->frame != NULL && s->frame->type != TYPE_ ## upctype) \
			vtc_fatal(vl, \
			    "Wrong frame type %s (%d) wanted %s", \
			    s->frame->type < TYPE_MAX ? \
			    h2_types[s->frame->type] : "?", \
			    s->frame->type, #upctype); \
	}

/* SECTION: stream.spec.prio_rxprio rxprio
 *
 * Receive a PRIORITY frame.
 */
RXFUNC(prio,	PRIORITY)

/* SECTION: stream.spec.reset_rxrst rxrst
 *
 * Receive a RST_STREAM frame.
 */
RXFUNC(rst,	RST_STREAM)

/* SECTION: stream.spec.settings_rxsettings rxsettings
 *
 * Receive a SETTINGS frame.
 */
RXFUNC(settings,SETTINGS)

/* SECTION: stream.spec.ping_rxping rxping
 *
 * Receive a PING frame.
 */
RXFUNC(ping,	PING)

/* SECTION: stream.spec.goaway_rxgoaway rxgoaway
 *
 * Receive a GOAWAY frame.
 */
RXFUNC(goaway,	GOAWAY)

/* SECTION: stream.spec.winup_rxwinup rxwinup
 *
 * Receive a WINDOW_UPDATE frame.
 */
RXFUNC(winup,	WINDOW_UPDATE)

/* SECTION: stream.spec.frame_rxframe
 *
 * Receive a frame, any frame.
 */
static void
cmd_rxframe(CMD_ARGS)
{
	struct stream *s;
	(void)cmd;
	(void)vl;
	(void)av;
	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);
	rxstuff(s);
}

static void
cmd_expect(CMD_ARGS)
{
	struct http *hp;
	struct stream *s;
	const char *lhs;
	char *cmp;
	const char *rhs;
	char buf[20];

	(void)cmd;
	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);
	hp = s->hp;
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	AZ(strcmp(av[0], "expect"));
	av++;

	AN(av[0]);
	AN(av[1]);
	AN(av[2]);
	AZ(av[3]);
	AZ(pthread_mutex_lock(&s->hp->mtx));
	lhs = cmd_var_resolve(s, av[0], buf);
	cmp = av[1];
	rhs = cmd_var_resolve(s, av[2], buf);
	vtc_expect(vl, av[0], lhs, cmp, av[2], rhs);
	AZ(pthread_mutex_unlock(&s->hp->mtx));
}

/* SECTION: stream.spec.write_body
 *
 * write_body STRING
 *	Same as the ``write_body`` command for HTTP/1.
 */
static void
cmd_write_body(CMD_ARGS)
{
	struct stream *s;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);
	AN(av[0]);
	AN(av[1]);
	AZ(av[2]);
	AZ(strcmp(av[0], "write_body"));
	if (VFIL_writefile(NULL, av[1], s->body, s->bodylen) != 0)
		vtc_fatal(s->hp->vl, "failed to write body: %s (%d)",
		    strerror(errno), errno);
}

/* SECTION: stream.spec Specification
 *
 * The specification of a stream follows the exact same rules as one for a
 * client or a server.
 */
static const struct cmds stream_cmds[] = {
#define CMD(n) { #n, cmd_##n },
#define CMD_STREAM(n) { #n, cmd_##n },
	/* spec */
	CMD_STREAM(expect)
	CMD_STREAM(rxcont)
	CMD_STREAM(rxdata)
	CMD_STREAM(rxframe)
	CMD_STREAM(rxgoaway)
	CMD_STREAM(rxhdrs)
	CMD_STREAM(rxping)
	CMD_STREAM(rxprio)
	CMD_STREAM(rxpush)
	CMD_STREAM(rxreq)
	CMD_STREAM(rxresp)
	CMD_STREAM(rxrst)
	CMD_STREAM(rxsettings)
	CMD_STREAM(rxwinup)
	CMD_STREAM(sendhex)
	CMD_STREAM(txcont)
	CMD_STREAM(txdata)
	CMD_STREAM(txgoaway)
	CMD_STREAM(txping)
	CMD_STREAM(txprio)
	CMD_STREAM(txpush)
	CMD_STREAM(txreq)
	CMD_STREAM(txresp)
	CMD_STREAM(txrst)
	CMD_STREAM(txsettings)
	CMD_STREAM(txwinup)
	CMD_STREAM(write_body)

	/* general purpose */
	CMD(barrier)
	CMD(delay)
	CMD(shell)
	{ NULL, NULL }
#undef CMD_STREAM
#undef CMD
};

static void *
stream_thread(void *priv)
{
	struct stream *s;

	CAST_OBJ_NOTNULL(s, priv, STREAM_MAGIC);

	parse_string(s->spec, stream_cmds, s, s->hp->vl);

	clean_headers(s->req);
	clean_headers(s->resp);
	vtc_log(s->hp->vl, 2, "Ending stream %u", s->id);
	return (NULL);
}
/**********************************************************************
 * Allocate and initialize a stream
 */

static struct stream *
stream_new(const char *name, struct http *h)
{
	char *p;
	struct stream *s;

	ALLOC_OBJ(s, STREAM_MAGIC);
	AN(s);
	AZ(pthread_cond_init(&s->cond, NULL));
	REPLACE(s->name, name);
	AN(name);
	VTAILQ_INIT(&s->fq);
	s->ws = h->iws;

	s->weight = 16;
	s->dependency = 0;

	STRTOU32(s->id, name, p, h->vl, "stream");
	if (s->id & (1U << 31))
		vtc_fatal(h->vl, "Stream id must be a 31-bits integer "
		    "(found %s)", name);

	CHECK_OBJ_NOTNULL(h, HTTP_MAGIC);
	s->hp = h;

	//bprintf(s->connect, "%s", "${v1_sock}");
	AZ(pthread_mutex_lock(&h->mtx));
	VTAILQ_INSERT_HEAD(&h->streams, s, list);
	AZ(pthread_mutex_unlock(&h->mtx));
	return (s);
}

/**********************************************************************
 * Clean up stream
 */

static void
stream_delete(struct stream *s)
{
	CHECK_OBJ_NOTNULL(s, STREAM_MAGIC);
	free(s->spec);
	free(s->name);
	FREE_OBJ(s);
}

/**********************************************************************
 * Start the stream thread
 */

static void
stream_start(struct stream *s)
{
	CHECK_OBJ_NOTNULL(s, STREAM_MAGIC);
	vtc_log(s->hp->vl, 2, "Starting stream %p", s);
	AZ(pthread_create(&s->tp, NULL, stream_thread, s));
	s->running = 1;
}

/**********************************************************************
 * Wait for stream thread to stop
 */
static void
stream_wait(struct stream *s)
{
	void *res;
	struct frame *f, *f2;

	CHECK_OBJ_NOTNULL(s, STREAM_MAGIC);
	vtc_log(s->hp->vl, 2, "Waiting for stream %u", s->id);
	AZ(pthread_join(s->tp, &res));
	if (res != NULL)
		vtc_fatal(s->hp->vl, "Stream %u returned \"%s\"", s->id,
		    (char *)res);

	VTAILQ_FOREACH_SAFE(f, &s->fq, list, f2)
		clean_frame(&f);
	clean_frame(&s->frame);
	s->tp = 0;
	s->running = 0;
}

/**********************************************************************
 * Run the stream thread
 */

static void
stream_run(struct stream *s)
{
	stream_start(s);
	stream_wait(s);
}



/* SECTION: client-server.spec.stream
 *
 * stream
 *	HTTP/2 introduces the concept of streams, and these come with
 *	their own specification, and as it's quite big, have been moved
 *	to their own chapter.
 *
 * SECTION: stream stream
 *
 * (note: this section is at the top-level for easier navigation, but
 * it's part of the client/server specification)
 *
 * Streams map roughly to a request in HTTP/2, a request is sent on
 * stream N, the response too, then the stream is discarded. The main
 * exception is the first stream, 0, that serves as coordinator.
 *
 * Stream syntax follow the client/server one::
 *
 *	stream ID [SPEC] [ACTION]
 *
 * ID is the HTTP/2 stream number, while SPEC describes what will be
 * done in that stream.
 *
 * Note that, when parsing a stream action, if the entity isn't operating
 * in HTTP/2 mode, these spec is ran before::
 *
 *	txpri/rxpri # client/server
 *	stream 0 {
 *	    txsettings
 *	    rxsettings
 *	    txsettings -ack
 *	    rxsettings
 *	    expect settings.ack == true
 *	} -run
 *
 * And HTTP/2 mode is then activated before parsing the specification.
 *
 * SECTION: stream.actions Actions
 *
 * \-start
 *	Run the specification in a thread, giving back control immediately.
 *
 * \-wait
 *	Wait for the started thread to finish running the spec.
 *
 * \-run
 *	equivalent to calling ``-start`` then ``-wait``.
 */

void
cmd_stream(CMD_ARGS)
{
	struct stream *s;
	struct http *h;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(h, priv, HTTP_MAGIC);

	AZ(strcmp(av[0], "stream"));
	av++;

	VTAILQ_FOREACH(s, &h->streams, list)
		if (!strcmp(s->name, av[0]))
			break;
	if (s == NULL)
		s = stream_new(av[0], h);
	av++;

	for (; *av != NULL; av++) {
		if (vtc_error)
			break;

		if (!strcmp(*av, "-wait")) {
			stream_wait(s);
			continue;
		}

		/* Don't muck about with a running client */
		if (s->running)
			stream_wait(s);

		if (!strcmp(*av, "-start")) {
			stream_start(s);
			continue;
		}
		if (!strcmp(*av, "-run")) {
			stream_run(s);
			continue;
		}
		if (**av == '-')
			vtc_fatal(vl, "Unknown client argument: %s", *av);
		REPLACE(s->spec, *av);
	}
}

void
b64_settings(const struct http *hp, const char *s)
{
	uint16_t i;
	uint64_t v;
	const char *buf;
	int shift;
	while (*s) {
		v = 0;
		for (shift = 42; shift >= 0; shift -= 6) {
			if (*s >= 'A' && *s <= 'Z')
				v |= (uint64_t)(*s - 'A') << shift;
			else if (*s >= 'a' && *s <= 'z')
				v |= (uint64_t)(*s - 'a' + 26) << shift;
			else if (*s >= '0' && *s <= '9')
				v |= (uint64_t)(*s - '0' + 52) << shift;
			else if (*s == '-')
				v |= (uint64_t)62 << shift;
			else if (*s == '_')
				v |= (uint64_t)63 << shift;
			else
				vtc_fatal(hp->vl,
				    "Bad \"HTTP2-Settings\" header");
			s++;
		}
		i = v >> 32;
		v &= 0xffff;

		if (i <= SETTINGS_MAX)
			buf = h2_settings[i];
		else
			buf = "unknown";

		if (v == 1) {
			if (hp->sfd)
				HPK_ResizeTbl(hp->encctx, v);
			else
				HPK_ResizeTbl(hp->decctx, v);
		}

		vtc_log(hp->vl, 4, "Upgrade: %s (%d): %ju",
		    buf, i, (intmax_t)v);
	}
}

void
start_h2(struct http *hp)
{
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	AZ(pthread_mutex_init(&hp->mtx, NULL));
	AZ(pthread_cond_init(&hp->cond, NULL));
	VTAILQ_INIT(&hp->streams);
	hp->iws = 0xffff;
	hp->ws = 0xffff;

	hp->h2 = 1;

	hp->decctx = HPK_NewCtx(4096);
	hp->encctx = HPK_NewCtx(4096);
	AZ(pthread_create(&hp->tp, NULL, receive_frame, hp));
}

void
stop_h2(struct http *hp)
{
	struct stream *s, *s2;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	VTAILQ_FOREACH_SAFE(s, &hp->streams, list, s2) {
		if (s->running)
			stream_wait(s);
		AZ(pthread_mutex_lock(&hp->mtx));
		VTAILQ_REMOVE(&hp->streams, s, list);
		AZ(pthread_mutex_unlock(&hp->mtx));
		stream_delete(s);
	}

	AZ(pthread_mutex_lock(&hp->mtx));
	hp->h2 = 0;
	AZ(pthread_cond_signal(&hp->cond));
	AZ(pthread_mutex_unlock(&hp->mtx));
	AZ(pthread_join(hp->tp, NULL));

	HPK_FreeCtx(hp->decctx);
	HPK_FreeCtx(hp->encctx);

	AZ(pthread_mutex_destroy(&hp->mtx));
	AZ(pthread_cond_destroy(&hp->cond));
}
