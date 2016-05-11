/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "vdef.h"
#include "vas.h"
#include "miniobj.h"
#include "vmb.h"

#include "vqueue.h"
#include "vre.h"
#include "vsl_priv.h"

#include "vapi/vsl.h"
#include "vapi/vsm.h"

#include "vsl_api.h"
#include "vsm_api.h"

struct vslc_vsm {
	unsigned			magic;
#define VSLC_VSM_MAGIC			0x4D3903A6

	struct VSL_cursor		cursor;

	unsigned			options;

	struct VSM_data			*vsm;
	struct VSM_fantom		vf;

	const struct VSL_head		*head;
	const uint32_t			*end;
	struct VSLC_ptr			next;
};

static void
vslc_vsm_delete(const struct VSL_cursor *cursor)
{
	struct vslc_vsm *c;

	CAST_OBJ_NOTNULL(c, cursor->priv_data, VSLC_VSM_MAGIC);
	assert(&c->cursor == cursor);
	FREE_OBJ(c);
}

/*
 * We tolerate the fact that segment_n wraps around eventually: for the default
 * vsl_space of 80MB and 8 segments, each segment is 10MB long, so we wrap
 * roughly after 40 pebibytes (32bit) or 160 yobibytes (64bit) worth of vsl
 * written.
 *
 * The vsm_check would fail if a vslc paused while this amount of data was
 * written
 */

static int
vslc_vsm_check(const struct VSL_cursor *cursor, const struct VSLC_ptr *ptr)
{
	const struct vslc_vsm *c;
	unsigned dist;

	CAST_OBJ_NOTNULL(c, cursor->priv_data, VSLC_VSM_MAGIC);
	assert(&c->cursor == cursor);

	if (ptr->ptr == NULL)
		return (0);

	dist = c->head->segment_n - ptr->priv;

	if (dist >= VSL_SEGMENTS - 2)
		/* Too close to continue */
		return (0);
	if (dist >= VSL_SEGMENTS - 4)
		/* Warning level */
		return (1);
	/* Safe */
	return (2);
}

static int
vslc_vsm_next(const struct VSL_cursor *cursor)
{
	struct vslc_vsm *c;
	int i;
	uint32_t t;

	CAST_OBJ_NOTNULL(c, cursor->priv_data, VSLC_VSM_MAGIC);
	assert(&c->cursor == cursor);
	CHECK_OBJ_NOTNULL(c->vsm, VSM_MAGIC);

	while (1) {
		i = vslc_vsm_check(&c->cursor, &c->next);
		if (i <= 0)
			return (-3); /* Overrun */

		t = *(volatile const uint32_t *)c->next.ptr;
		AN(t);

		if (t == VSL_WRAPMARKER) {
			/* Wrap around not possible at front */
			assert(c->next.ptr != c->head->log);
			c->next.ptr = c->head->log;
			while (c->next.priv % VSL_SEGMENTS)
				c->next.priv++;
			continue;
		}

		if (t == VSL_ENDMARKER) {
			if (VSM_invalid == VSM_StillValid(c->vsm, &c->vf) ||
			    VSM_Abandoned(c->vsm))
				return (-2); /* VSL abandoned */
			if (c->options & VSL_COPT_TAILSTOP)
				return (-1); /* EOF */
			return (0);	/* No new records available */
		}

		c->cursor.rec = c->next;
		c->next.ptr = VSL_NEXT(c->next.ptr);

		if (VSL_TAG(c->cursor.rec.ptr) == SLT__Batch) {
			if (!(c->options & VSL_COPT_BATCH))
				/* Skip the batch record */
				continue;
			/* Next call will point to the first record past
			   the batch */
			c->next.ptr +=
			    VSL_WORDS(VSL_BATCHLEN(c->cursor.rec.ptr));
		}

		while ((c->next.ptr - c->head->log) / c->head->segsize >
		    c->next.priv % VSL_SEGMENTS)
			c->next.priv++;

		assert(c->next.ptr >= c->head->log);
		assert(c->next.ptr < c->end);

		return (1);
	}
}

static int
vslc_vsm_reset(const struct VSL_cursor *cursor)
{
	struct vslc_vsm *c;
	unsigned u, segment_n;
	int i;

	CAST_OBJ_NOTNULL(c, cursor->priv_data, VSLC_VSM_MAGIC);
	assert(&c->cursor == cursor);
	c->cursor.rec.ptr = NULL;

	segment_n = c->head->segment_n;
	VRMB();			/* Make sure offset table is not stale
				   compared to segment_n */

	if (c->options & VSL_COPT_TAIL) {
		/* Start in the same segment varnishd currently is in and
		   run forward until we see the end */
		u = c->next.priv = segment_n;
		assert(c->head->offset[c->next.priv % VSL_SEGMENTS] >= 0);
		c->next.ptr = c->head->log +
		    c->head->offset[c->next.priv % VSL_SEGMENTS];
		do {
			if (c->head->segment_n - u > 1) {
				/* Give up if varnishd is moving faster
				   than us */
				return (-3); /* overrun */
			}
			i = vslc_vsm_next(&c->cursor);
		} while (i == 1);
		if (i)
			return (i);
	} else {
		/* Starting (VSL_SEGMENTS - 3) behind varnishd. This way
		 * even if varnishd advances segment_n immediately, we'll
		 * still have a full segment worth of log before the
		 * general constraint of at least 2 segments apart will be
		 * broken.
		 */
		c->next.priv = segment_n - (VSL_SEGMENTS - 3);
		while (c->head->offset[c->next.priv % VSL_SEGMENTS] < 0) {
			/* seg 0 must be initialized */
			assert(c->next.priv % VSL_SEGMENTS != 0);
			c->next.priv++;
		}
		assert(c->head->offset[c->next.priv % VSL_SEGMENTS] >= 0);
		c->next.ptr = c->head->log +
		    c->head->offset[c->next.priv % VSL_SEGMENTS];
	}
	assert(c->next.ptr >= c->head->log);
	assert(c->next.ptr < c->end);
	return (0);
}

static const struct vslc_tbl vslc_vsm_tbl = {
	.magic		= VSLC_TBL_MAGIC,
	.delete		= vslc_vsm_delete,
	.next		= vslc_vsm_next,
	.reset		= vslc_vsm_reset,
	.check		= vslc_vsm_check,
};

struct VSL_cursor *
VSL_CursorVSM(struct VSL_data *vsl, struct VSM_data *vsm, unsigned options)
{
	struct vslc_vsm *c;
	struct VSM_fantom vf;
	struct VSL_head *head;
	int i;

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	CHECK_OBJ_NOTNULL(vsm, VSM_MAGIC);

	if (!VSM_Get(vsm, &vf, VSL_CLASS, "", "")) {
		(void)vsl_diag(vsl,
		    "No VSL chunk found (child not started ?)\n");
		return (NULL);
	}

	head = vf.b;
	if (memcmp(head->marker, VSL_HEAD_MARKER, sizeof head->marker)) {
		(void)vsl_diag(vsl, "Not a VSL chunk\n");
		return (NULL);
	}
	ALLOC_OBJ(c, VSLC_VSM_MAGIC);
	if (c == NULL) {
		(void)vsl_diag(vsl, "Out of memory\n");
		return (NULL);
	}
	c->cursor.priv_tbl = &vslc_vsm_tbl;
	c->cursor.priv_data = c;

	c->options = options;
	c->vsm = vsm;
	c->vf = vf;
	c->head = head;
	c->end = c->head->log + c->head->segsize * VSL_SEGMENTS;
	assert(c->end <= (const uint32_t *)vf.e);

	i = vslc_vsm_reset(&c->cursor);
	if (i) {
		(void)vsl_diag(vsl, "Cursor initialization failure (%d)\n", i);
		FREE_OBJ(c);
		return (NULL);
	}

	return (&c->cursor);
}

struct vslc_file {
	unsigned			magic;
#define VSLC_FILE_MAGIC			0x1D65FFEF

	int				error;
	int				fd;
	int				close_fd;
	ssize_t				buflen;
	uint32_t			*buf;

	struct VSL_cursor		cursor;

};

static void
vslc_file_delete(const struct VSL_cursor *cursor)
{
	struct vslc_file *c;

	CAST_OBJ_NOTNULL(c, cursor->priv_data, VSLC_FILE_MAGIC);
	assert(&c->cursor == cursor);
	if (c->close_fd)
		(void)close(c->fd);
	if (c->buf != NULL)
		free(c->buf);
	FREE_OBJ(c);
}

/* Read n bytes from fd into buf */
static ssize_t
vslc_file_readn(int fd, void *buf, size_t n)
{
	size_t t = 0;
	ssize_t l;

	while (t < n) {
		l = read(fd, (char *)buf + t, n - t);
		if (l <= 0)
			return (l);
		t += l;
	}
	return (t);
}

static int
vslc_file_next(const struct VSL_cursor *cursor)
{
	struct vslc_file *c;
	ssize_t i;
	size_t l;

	CAST_OBJ_NOTNULL(c, cursor->priv_data, VSLC_FILE_MAGIC);
	assert(&c->cursor == cursor);

	if (c->error)
		return (c->error);

	do {
		c->cursor.rec.ptr = NULL;
		assert(c->buflen >= 2);
		i = vslc_file_readn(c->fd, c->buf, VSL_BYTES(2));
		if (i < 0)
			return (-4);	/* I/O error */
		if (i == 0)
			return (-1);	/* EOF */
		assert(i == VSL_BYTES(2));
		l = 2 + VSL_WORDS(VSL_LEN(c->buf));
		if (c->buflen < l) {
			while (c->buflen < l)
				c->buflen = 2 * l;
			c->buf = realloc(c->buf, VSL_BYTES(c->buflen));
			AN(c->buf);
		}
		if (l > 2) {
			i = vslc_file_readn(c->fd, c->buf + 2,
			    VSL_BYTES(l - 2));
			if (i < 0)
				return (-4);	/* I/O error */
			if (i == 0)
				return (-1);	/* EOF */
			assert(i == VSL_BYTES(l - 2));
		}
		c->cursor.rec.ptr = c->buf;
	} while (VSL_TAG(c->cursor.rec.ptr) == SLT__Batch);
	return (1);
}

static int
vslc_file_reset(const struct VSL_cursor *cursor)
{
	(void)cursor;
	/* XXX: Implement me */
	return (-1);
}

static const struct vslc_tbl vslc_file_tbl = {
	.magic		= VSLC_TBL_MAGIC,
	.delete		= vslc_file_delete,
	.next		= vslc_file_next,
	.reset		= vslc_file_reset,
	.check		= NULL,
};

struct VSL_cursor *
VSL_CursorFile(struct VSL_data *vsl, const char *name, unsigned options)
{
	struct vslc_file *c;
	int fd;
	int close_fd = 0;
	char buf[] = VSL_FILE_ID;
	ssize_t i;

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	AN(name);
	(void)options;

	if (!strcmp(name, "-"))
		fd = STDIN_FILENO;
	else {
		fd = open(name, O_RDONLY);
		if (fd < 0) {
			vsl_diag(vsl, "Could not open %s: %s\n", name,
			    strerror(errno));
			return (NULL);
		}
		close_fd = 1;
	}

	i = vslc_file_readn(fd, buf, sizeof buf);
	if (i <= 0) {
		if (close_fd)
			(void)close(fd);
		vsl_diag(vsl, "VSL file read error: %s\n",
		    i < 0 ? strerror(errno) : "EOF");
		return (NULL);
	}
	assert(i == sizeof buf);
	if (memcmp(buf, VSL_FILE_ID, sizeof buf)) {
		if (close_fd)
			(void)close(fd);
		vsl_diag(vsl, "Not a VSL file: %s\n", name);
		return (NULL);
	}

	ALLOC_OBJ(c, VSLC_FILE_MAGIC);
	if (c == NULL) {
		if (close_fd)
			(void)close(fd);
		vsl_diag(vsl, "Out of memory\n");
		return (NULL);
	}
	c->cursor.priv_tbl = &vslc_file_tbl;
	c->cursor.priv_data = c;

	c->fd = fd;
	c->close_fd = close_fd;
	c->buflen = VSL_WORDS(BUFSIZ);
	c->buf = malloc(VSL_BYTES(c->buflen));
	AN(c->buf);

	return (&c->cursor);
}

void
VSL_DeleteCursor(const struct VSL_cursor *cursor)
{
	const struct vslc_tbl *tbl;

	CAST_OBJ_NOTNULL(tbl, cursor->priv_tbl, VSLC_TBL_MAGIC);
	if (tbl->delete == NULL)
		return;
	(tbl->delete)(cursor);
}

int
VSL_ResetCursor(const struct VSL_cursor *cursor)
{
	const struct vslc_tbl *tbl;

	CAST_OBJ_NOTNULL(tbl, cursor->priv_tbl, VSLC_TBL_MAGIC);
	if (tbl->reset == NULL)
		return (-1);
	return ((tbl->reset)(cursor));
}

int
VSL_Next(const struct VSL_cursor *cursor)
{
	const struct vslc_tbl *tbl;

	CAST_OBJ_NOTNULL(tbl, cursor->priv_tbl, VSLC_TBL_MAGIC);
	AN(tbl->next);
	return ((tbl->next)(cursor));
}

int
VSL_Check(const struct VSL_cursor *cursor, const struct VSLC_ptr *ptr)
{
	const struct vslc_tbl *tbl;

	CAST_OBJ_NOTNULL(tbl, cursor->priv_tbl, VSLC_TBL_MAGIC);
	if (tbl->check == NULL)
		return (-1);
	return ((tbl->check)(cursor, ptr));
}
