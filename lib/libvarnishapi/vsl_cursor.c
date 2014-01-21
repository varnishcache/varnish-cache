/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "vas.h"
#include "miniobj.h"
#include "vapi/vsm.h"
#include "vsm_api.h"
#include "vapi/vsl.h"
#include "vsl_api.h"

struct vslc_vsm {
	unsigned			magic;
#define VSLC_VSM_MAGIC			0x4D3903A6

	struct VSL_cursor		cursor;

	unsigned			options;

	struct VSM_data			*vsm;
	struct VSM_fantom		vf;

	const struct VSL_head		*head;
	const uint32_t			*end;
	ssize_t				segsize;
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

static int
vslc_vsm_check(const struct VSL_cursor *cursor, const struct VSLC_ptr *ptr)
{
	const struct vslc_vsm *c;
	unsigned seqdiff, segment, segdiff;

	CAST_OBJ_NOTNULL(c, cursor->priv_data, VSLC_VSM_MAGIC);
	assert(&c->cursor == cursor);

	if (ptr->ptr == NULL)
		return (0);

	/* Check sequence number */
	seqdiff = c->head->seq - ptr->priv;
	if (c->head->seq < ptr->priv)
		/* Wrap around skips 0 */
		seqdiff -= 1;
	if (seqdiff > 1)
		/* Too late */
		return (0);

	/* Check overrun */
	segment = (ptr->ptr - c->head->log) / c->segsize;
	if (segment >= VSL_SEGMENTS)
		/* Rounding error spills to last segment */
		segment = VSL_SEGMENTS - 1;
	segdiff = (segment - c->head->segment) % VSL_SEGMENTS;
	if (segdiff == 0 && seqdiff == 0)
		/* In same segment, but close to tail */
		return (2);
	if (segdiff <= 2)
		/* Too close to continue */
		return (0);
	if (segdiff <= 4)
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

	/* Assert pointers */
	AN(c->next.ptr);
	assert(c->next.ptr >= c->head->log);
	assert(c->next.ptr < c->end);

	i = vslc_vsm_check(&c->cursor, &c->next);
	if (i <= 0)
		/* Overrun */
		return (-3);

	/* Check VSL fantom and abandonment */
	if (*(volatile const uint32_t *)c->next.ptr == VSL_ENDMARKER) {
		if (VSM_invalid == VSM_StillValid(c->vsm, &c->vf) ||
		    VSM_Abandoned(c->vsm))
			return (-2);
	}

	while (1) {
		assert(c->next.ptr >= c->head->log);
		assert(c->next.ptr < c->end);
		AN(c->head->seq);
		t = *(volatile const uint32_t *)c->next.ptr;
		AN(t);

		if (t == VSL_WRAPMARKER) {
			/* Wrap around not possible at front */
			assert(c->next.ptr != c->head->log);
			c->next.ptr = c->head->log;
			continue;
		}

		if (t == VSL_ENDMARKER) {
			if (c->next.ptr != c->head->log &&
			    c->next.priv != c->head->seq) {
				/* ENDMARKER not at front and seq wrapped */
				/* XXX: assert on this? */
				c->next.ptr = c->head->log;
				continue;
			}
			if (c->options & VSL_COPT_TAILSTOP)
				/* EOF */
				return (-1);
			else
				return (0);
		}

		if (c->next.ptr == c->head->log)
			c->next.priv = c->head->seq;

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
		return (1);
	}
}

static int
vslc_vsm_reset(const struct VSL_cursor *cursor)
{
	struct vslc_vsm *c;
	unsigned segment;

	CAST_OBJ_NOTNULL(c, cursor->priv_data, VSLC_VSM_MAGIC);
	assert(&c->cursor == cursor);

	/*
	 * Starting (VSL_SEGMENTS - 3) behind varnishd. This way
	 * even if varnishd wraps immediately, we'll still have a
	 * full segment worth of log before the general constraint
	 * of at least 2 segments apart will be broken
	 */
	segment = (c->head->segment + 3) % VSL_SEGMENTS;
	if (c->head->segments[segment] < 0)
		segment = 0;
	assert(c->head->segments[segment] >= 0);
	c->next.ptr = c->head->log + c->head->segments[segment];
	c->next.priv = c->head->seq;
	c->cursor.rec.ptr = NULL;

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

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	CHECK_OBJ_NOTNULL(vsm, VSM_MAGIC);

	if (!VSM_Get(vsm, &vf, VSL_CLASS, "", "")) {
		vsl_diag(vsl, "No VSL chunk found (child not started ?)\n");
		return (NULL);
	}

	head = vf.b;
	if (memcmp(head->marker, VSL_HEAD_MARKER, sizeof head->marker)) {
		vsl_diag(vsl, "Not a VSL chunk\n");
		return (NULL);
	}
	if (head->seq == 0) {
		vsl_diag(vsl, "VSL chunk not initialized\n");
		return (NULL);
	}

	ALLOC_OBJ(c, VSLC_VSM_MAGIC);
	if (c == NULL) {
		vsl_diag(vsl, "Out of memory\n");
		return (NULL);
	}
	c->cursor.priv_tbl = &vslc_vsm_tbl;
	c->cursor.priv_data = c;

	c->options = options;
	c->vsm = vsm;
	c->vf = vf;
	c->head = head;
	c->end = vf.e;
	c->segsize = (c->end - c->head->log) / VSL_SEGMENTS;

	if (c->options & VSL_COPT_TAIL) {
		/* Locate tail of log */
		c->next.ptr = c->head->log +
		    c->head->segments[c->head->segment];
		while (c->next.ptr < c->end &&
		    *(volatile const uint32_t *)c->next.ptr != VSL_ENDMARKER)
			c->next.ptr = VSL_NEXT(c->next.ptr);
		c->next.priv = c->head->seq;
	} else
		AZ(vslc_vsm_reset(&c->cursor));

	return (&c->cursor);
}

struct vslc_file {
	unsigned			magic;
#define VSLC_FILE_MAGIC			0x1D65FFEF

	struct VSL_cursor		cursor;

	int				error;
	int				fd;
	int				close_fd;
	ssize_t				buflen;
	uint32_t			*buf;
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
