/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2013 Varnish Software AS
 * All rights reserved.
 *
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

#include "vas.h"
#include "miniobj.h"
#include "vqueue.h"
#include "vtree.h"
#include "vtim.h"

#include "vapi/vsl.h"
#include "vsl_api.h"

#define VTX_CACHE 10
#define VTX_BUFSIZE_MIN 64

enum vtx_type_e {
	vtx_t_unknown,
	vtx_t_sess,
	vtx_t_req,
	vtx_t_esireq,
	vtx_t_bereq,
};

enum vtx_link_e {
	vtx_l_sess,
	vtx_l_req,
	vtx_l_esireq,
	vtx_l_bereq,
};

struct vtx_key {
	unsigned		vxid;
	VRB_ENTRY(vtx_key)	entry;
};
VRB_HEAD(vtx_tree, vtx_key);

struct vtx {
	struct vtx_key		key;
	unsigned		magic;
#define VTX_MAGIC		0xACC21D09
	VTAILQ_ENTRY(vtx)	list_child;
	VTAILQ_ENTRY(vtx)	list_incomplete;

	double			t_start;
	unsigned		flags;
#define VTX_F_SHM		0x1
#define VTX_F_COMPLETE		0x2
#define VTX_F_READY		0x4

	enum vtx_type_e		type;

	struct vtx		*parent;
	VTAILQ_HEAD(,vtx)	child;
	unsigned		n_child;
	unsigned		n_childready;
	unsigned		n_descend;

	const uint32_t		*start;
	ssize_t			len;
	unsigned		index;

	uint32_t		*buf;
	ssize_t			bufsize;
};

struct vslc_raw {
	struct vslc		c;
	unsigned		magic;
#define VSLC_RAW_MAGIC		0x247EBD44

	const uint32_t		*start;
	ssize_t			len;
	const uint32_t		*next;
};

struct vslc_vtx {
	struct vslc		c;
	unsigned		magic;
#define VSLC_VTX_MAGIC		0x74C6523F

	struct vtx		*vtx;
	const uint32_t		*next;
};

struct VSLQ {
	unsigned		magic;
#define VSLQ_MAGIC		0x23A8BE97

	struct VSL_data		*vsl;
	struct VSL_cursor	*c;
	struct vslq_query	*query;

	enum VSL_grouping_e	grouping;

	struct vtx_tree		tree;

	VTAILQ_HEAD(,vtx)	incomplete;
	unsigned		n_incomplete;

	VTAILQ_HEAD(,vtx)	cache;
	unsigned		n_cache;
};

static inline int
vtx_keycmp(const struct vtx_key *a, const struct vtx_key *b)
{
	if (a->vxid < b->vxid)
		return (-1);
	if (a->vxid > b->vxid)
		return (1);
	return (0);
}

VRB_PROTOTYPE(vtx_tree, vtx_key, entry, vtx_keycmp);
VRB_GENERATE(vtx_tree, vtx_key, entry, vtx_keycmp);

static int
vtx_diag(struct vtx *vtx, const char *fmt, ...)
{
	va_list ap;

	/* XXX: Prepend diagnostic message on vtx as a synthetic log
	   record. For now print to stderr */
	fprintf(stderr, "vtx_diag <%u>: ", vtx->key.vxid);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");

	return (-1);
}

static int
vtx_diag_tag(struct vtx *vtx, const uint32_t *ptr, const char *reason)
{
	return (vtx_diag(vtx, "%s (%s: %.*s)", reason, VSL_tags[VSL_TAG(ptr)],
		(int)VSL_LEN(ptr), VSL_CDATA(ptr)));
}

static int
vslc_raw_next(void *cursor)
{
	struct vslc_raw *c;

	CAST_OBJ_NOTNULL(c, cursor, VSLC_RAW_MAGIC);

	assert(c->next >= c->start);
	assert(c->next <= c->start + c->len);
	if (c->next < c->start + c->len) {
		c->c.c.ptr = c->next;
		c->next = VSL_NEXT(c->next);
		return (1);
	}
	return (0);
}

static int
vslc_raw_reset(void *cursor)
{
	struct vslc_raw *c;

	CAST_OBJ_NOTNULL(c, cursor, VSLC_RAW_MAGIC);

	assert(c->next >= c->start);
	assert(c->next <= c->start + c->len);
	c->next = c->start;
	c->c.c.ptr = NULL;

	return (0);
}

static int
vslc_vtx_next(void *cursor)
{
	struct vslc_vtx *c;

	CAST_OBJ_NOTNULL(c, cursor, VSLC_VTX_MAGIC);

	assert(c->next >= c->vtx->start);
	assert(c->next <= c->vtx->start + c->vtx->len);
	if (c->next < c->vtx->start + c->vtx->len) {
		c->c.c.ptr = c->next;
		c->next = VSL_NEXT(c->next);
		return (1);
	}
	return (0);
}

static int
vslc_vtx_reset(void *cursor)
{
	struct vslc_vtx *c;

	CAST_OBJ_NOTNULL(c, cursor, VSLC_VTX_MAGIC);

	assert(c->next >= c->vtx->start);
	assert(c->next <= c->vtx->start + c->vtx->len);
	c->next = c->vtx->start;
	c->c.c.ptr = NULL;

	return (0);
}

static void
vslc_vtx_setup(struct vslc_vtx *c, struct vtx *vtx, unsigned level)
{
	AN(c);
	AN(vtx);

	memset(c, 0, sizeof *c);
	c->c.c.vxid = vtx->key.vxid;
	c->c.c.level = level;
	c->c.magic = VSLC_MAGIC;
	c->c.next = vslc_vtx_next;
	c->c.reset = vslc_vtx_reset;

	c->magic = VSLC_VTX_MAGIC;
	c->vtx = vtx;
	c->next = c->vtx->start;
}

static struct vtx *
vtx_new(struct VSLQ *vslq)
{
	struct vtx *vtx;

	AN(vslq);
	if (vslq->n_cache) {
		AZ(VTAILQ_EMPTY(&vslq->cache));
		vtx = VTAILQ_FIRST(&vslq->cache);
		VTAILQ_REMOVE(&vslq->cache, vtx, list_child);
		vslq->n_cache--;
	} else {
		ALLOC_OBJ(vtx, VTX_MAGIC);
		AN(vtx);
	}

	vtx->key.vxid = 0;
	vtx->t_start = VTIM_mono();
	vtx->flags = 0;
	vtx->type = vtx_t_unknown;
	vtx->parent = NULL;
	VTAILQ_INIT(&vtx->child);
	vtx->n_child = 0;
	vtx->n_childready = 0;
	vtx->n_descend = 0;
	vtx->start = vtx->buf;
	vtx->len = 0;
	vtx->index = 0;

	VTAILQ_INSERT_TAIL(&vslq->incomplete, vtx, list_incomplete);
	vslq->n_incomplete++;

	return (vtx);
}

static void
vtx_free(struct vtx **pvtx)
{
	struct vtx *vtx;

	AN(pvtx);
	vtx = *pvtx;
	*pvtx = NULL;

	free(vtx->buf);
	FREE_OBJ(vtx);
}

static void
vtx_retire(struct VSLQ *vslq, struct vtx **pvtx)
{
	struct vtx *vtx;
	struct vtx *child;

	AN(vslq);
	AN(pvtx);

	vtx = *pvtx;
	*pvtx = NULL;
	CHECK_OBJ_NOTNULL(vtx, VTX_MAGIC);

	AN(vtx->flags & VTX_F_COMPLETE);
	AN(vtx->flags & VTX_F_READY);
	AZ(vtx->parent);

	while (!VTAILQ_EMPTY(&vtx->child)) {
		child = VTAILQ_FIRST(&vtx->child);
		assert(child->parent == vtx);
		AN(vtx->n_child);
		assert(vtx->n_descend >= child->n_descend + 1);
		VTAILQ_REMOVE(&vtx->child, child, list_child);
		child->parent = NULL;
		vtx->n_child--;
		vtx->n_descend -= child->n_descend + 1;
		vtx_retire(vslq, &child);
		AZ(child);
	}
	AZ(vtx->n_child);
	AZ(vtx->n_descend);
	AN(VRB_REMOVE(vtx_tree, &vslq->tree, &vtx->key));

	if (vslq->n_cache < VTX_CACHE) {
		VTAILQ_INSERT_HEAD(&vslq->cache, vtx, list_child);
		vslq->n_cache++;
	} else {
		vtx_free(&vtx);
		AZ(vtx);
	}
}

static struct vtx *
vtx_lori(struct VSLQ *vslq, unsigned vxid)
{
	struct vtx *vtx;
	struct vtx_key lkey, *key;

	AN(vslq);
	lkey.vxid = vxid;
	key = VRB_FIND(vtx_tree, &vslq->tree, &lkey);
	if (key != NULL) {
		CAST_OBJ_NOTNULL(vtx, (void *)key, VTX_MAGIC);
		return (vtx);
	}

	vtx = vtx_new(vslq);
	AN(vtx);
	vtx->key.vxid = vxid;
	AZ(VRB_INSERT(vtx_tree, &vslq->tree, &vtx->key));
	return (vtx);
}

static void
vtx_append(struct vtx *vtx, const uint32_t *ptr, ssize_t len, int shmptr_ok)
{
	ssize_t bufsize;
	const uint32_t *ptr2;
	ssize_t len2;

	AN(vtx);

	if (vtx->flags & VTX_F_SHM) {
		assert(vtx->start != vtx->buf);
		ptr2 = vtx->start;
		vtx->start = vtx->buf;
		len2 = vtx->len;
		vtx->len = 0;
		vtx->flags &= ~VTX_F_SHM;
		vtx_append(vtx, ptr2, len2, 0);
	}

	if (len == 0)
		return;
	AN(ptr);

	if (shmptr_ok && vtx->len == 0) {
		vtx->start = ptr;
		vtx->len = len;
		vtx->flags |= VTX_F_SHM;
		return;
	}

	bufsize = vtx->bufsize;
	if (bufsize == 0)
		bufsize = VTX_BUFSIZE_MIN;
	while (vtx->len + len > bufsize)
		bufsize *= 2;
	if (bufsize != vtx->bufsize) {
		vtx->buf = realloc(vtx->buf, 4 * bufsize);
		AN(vtx->buf);
		vtx->bufsize = bufsize;
		vtx->start = vtx->buf;
	}
	memcpy(&vtx->buf[vtx->len], ptr, 4 * len);
	vtx->len += len;
}

static struct vtx *
vtx_check_ready(struct VSLQ *vslq, struct vtx *vtx)
{
	struct vtx *ready;

	AN(vslq);
	AN(vtx->flags & VTX_F_COMPLETE);
	AZ(vtx->flags & VTX_F_READY);

	if (vtx->type == vtx_t_unknown)
		vtx_diag(vtx, "vtx of unknown type marked complete");

	ready = vtx;
	while (1) {
		if (ready->flags & VTX_F_COMPLETE &&
		    ready->n_child == ready->n_childready)
			ready->flags |= VTX_F_READY;
		else
			break;
		if (ready->parent == NULL)
			break;
		ready = ready->parent;
		ready->n_childready++;
		assert(ready->n_child >= ready->n_childready);
	}

	if (ready->flags & VTX_F_READY && ready->parent == NULL)
		/* Top level vtx ready */
		return (ready);

	if (vtx->flags & VTX_F_SHM) {
		/* Not ready, append zero to make sure it's not a shm
		   reference */
		vtx_append(vtx, NULL, 0, 0);
		AZ(vtx->flags & VTX_F_SHM);
	}

	return (NULL);
}

static int
vtx_parsetag_bl(const char *str, unsigned strlen, enum vtx_type_e *ptype,
    unsigned *pvxid)
{
	char ibuf[strlen + 1];
	char tbuf[7];
	unsigned vxid;
	int i;
	enum vtx_type_e type = vtx_t_unknown;

	AN(str);
	memcpy(ibuf, str, strlen);
	ibuf[strlen] = '\0';
	i = sscanf(ibuf, "%6s %u", tbuf, &vxid);
	if (i < 1)
		return (-1);
	if (!strcmp(tbuf, "sess"))
		type = vtx_t_sess;
	else if (!strcmp(tbuf, "req"))
		type = vtx_t_req;
	else if (!strcmp(tbuf, "esireq"))
		type = vtx_t_esireq;
	else if (!strcmp(tbuf, "bereq"))
		type = vtx_t_bereq;
	else
		return (-1);
	if (i == 1)
		vxid = 0;
	if (ptype)
		*ptype = type;
	if (pvxid)
		*pvxid = vxid;
	return (i);
}

static void
vtx_set_parent(struct vtx *parent, struct vtx *child)
{

	AN(parent);
	AN(child);
	AZ(child->parent);
	child->parent = parent;
	VTAILQ_INSERT_TAIL(&parent->child, child, list_child);
	parent->n_child++;
	do
		parent->n_descend += 1 + child->n_descend;
	while ((parent = parent->parent));
}

static int
vtx_scan_begintag(struct VSLQ *vslq, struct vtx *vtx, const uint32_t *ptr)
{
	int i;
	enum vtx_type_e p_type;
	unsigned p_vxid;
	struct vtx *p_vtx;

	assert(VSL_TAG(ptr) == SLT_Begin);

	if (vtx->flags & VTX_F_READY)
		return (vtx_diag_tag(vtx, ptr, "link too late"));

	i = vtx_parsetag_bl(VSL_CDATA(ptr), VSL_LEN(ptr), &p_type, &p_vxid);
	if (i < 1)
		return (vtx_diag_tag(vtx, ptr, "parse error"));

	/* Check/set vtx type */
	assert(p_type != vtx_t_unknown);
	if (vtx->type != vtx_t_unknown && vtx->type != p_type)
		return (vtx_diag_tag(vtx, ptr, "type mismatch"));
	vtx->type = p_type;

	if (i == 1 || p_vxid == 0)
		return (0);

	/* Lookup and check parent vtx */
	p_vtx = vtx_lori(vslq, p_vxid);
	AN(p_vtx);
	if (vtx->parent == p_vtx)
		/* Link already exists */
		return (0);
	if (vtx->parent != NULL)
		return (vtx_diag_tag(vtx, ptr, "duplicate link"));
	if (p_vtx->flags & VTX_F_READY)
		return (vtx_diag_tag(vtx, ptr, "link too late"));

	vtx_set_parent(p_vtx, vtx);

	return (0);
}

static int
vtx_scan_linktag(struct VSLQ *vslq, struct vtx *vtx, const uint32_t *ptr)
{
	int i;
	enum vtx_type_e c_type;
	unsigned c_vxid;
	struct vtx *c_vtx;

	assert(VSL_TAG(ptr) == SLT_Link);

	if (vtx->flags & VTX_F_READY)
		return (vtx_diag_tag(vtx, ptr, "link too late"));

	i = vtx_parsetag_bl(VSL_CDATA(ptr), VSL_LEN(ptr), &c_type, &c_vxid);
	if (i < 2)
		return (vtx_diag_tag(vtx, ptr, "parse error"));
	assert(i == 2);

	/* Lookup and check child vtx */
	c_vtx = vtx_lori(vslq, c_vxid);
	AN(c_vtx);
	if (c_vtx->parent == vtx)
		/* Link already exists */
		return (0);
	if (c_vtx->parent != NULL)
		return (vtx_diag_tag(vtx, ptr, "duplicate link"));
	if (c_vtx->flags & VTX_F_READY)
		return (vtx_diag_tag(vtx, ptr, "link too late"));
	assert(c_type != vtx_t_unknown);
	if (c_vtx->type != vtx_t_unknown && c_vtx->type != c_type)
		return (vtx_diag_tag(vtx, ptr, "type mismatch"));
	c_vtx->type = c_type;

	vtx_set_parent(vtx, c_vtx);

	return (0);
}

static struct vtx *
vtx_scan(struct VSLQ *vslq, struct vtx *vtx)
{
	const uint32_t *ptr;
	enum VSL_tag_e tag;
	int complete;

	complete = (vtx->flags & VTX_F_COMPLETE ? 1 : 0);
	ptr = vtx->start + vtx->index;
	assert(ptr <= vtx->start + vtx->len);
	for (; ptr < vtx->start + vtx->len; ptr = VSL_NEXT(ptr)) {
		tag = VSL_TAG(ptr);

		if (complete) {
			vtx_diag(vtx, "late log rec");
			continue;
		}

		if (vtx->type == vtx_t_unknown && tag != SLT_Begin)
			vtx_diag_tag(vtx, ptr, "early log rec");

		switch (tag) {
		case SLT_Begin:
			(void)vtx_scan_begintag(vslq, vtx, ptr);
			break;

		case SLT_Link:
			(void)vtx_scan_linktag(vslq, vtx, ptr);
			break;

		case SLT_End:
			complete = 1;
			break;

		default:
			break;
		}
	}
	vtx->index = ptr - vtx->start;
	assert(vtx->index <= vtx->len);

	if (!complete && vtx->flags & VTX_F_SHM) {
		/* Append zero to make sure it's not a shm reference */
		vtx_append(vtx, NULL, 0, 0);
		AZ(vtx->flags & VTX_F_SHM);
	}

	if (complete) {
		VTAILQ_REMOVE(&vslq->incomplete, vtx, list_incomplete);
		vtx->flags |= VTX_F_COMPLETE;
		AN(vslq->n_incomplete);
		vslq->n_incomplete--;
		return (vtx_check_ready(vslq, vtx));
	}

	return (NULL);
}

static struct vtx *
vtx_force(struct VSLQ *vslq, struct vtx *vtx, const char *reason)
{
	AZ(vtx->flags & VTX_F_COMPLETE);
	AZ(vtx->flags & VTX_F_READY);
	vtx_diag(vtx, reason);

	VTAILQ_REMOVE(&vslq->incomplete, vtx, list_incomplete);
	vtx->flags |= VTX_F_COMPLETE;
	AN(vslq->n_incomplete);
	vslq->n_incomplete--;

	return (vtx_check_ready(vslq, vtx));
}

static int
vslq_callback(struct VSLQ *vslq, struct vtx *vtx, VSLQ_dispatch_f *func,
    void *priv)
{
	unsigned n = vtx->n_descend + 1;
	unsigned i, j;
	struct vslc_vtx c[n];
	struct VSL_cursor *cp[n + 1];

	AN(vslq);
	CHECK_OBJ_NOTNULL(vtx, VTX_MAGIC);

	if (func == NULL)
		return (0);
	if (vslq->grouping == VSL_g_session &&
	    vtx->type != vtx_t_sess)
		return (0);
	if (vslq->grouping == VSL_g_request &&
	    vtx->type != vtx_t_req)
		return (0);

	i = j = 0;
	vslc_vtx_setup(&c[i], vtx, 0);
	i++;
	while (j < i) {
		vtx = VTAILQ_FIRST(&c[j].vtx->child);
		while (vtx) {
			assert(i < n);
			vslc_vtx_setup(&c[i], vtx, c[j].c.c.level + 1);
			i++;
			vtx = VTAILQ_NEXT(vtx, list_child);
		}
		j++;
	}
	assert(i == n);

	/* Reverse order */
	for (i = 0; i < n; i++)
		cp[i] = &c[n - i - 1].c.c;
	cp[i] = NULL;

	/* Query test goes here */
	if (vslq->query == NULL ? 1 : vslq_runquery(vslq->query, cp))
		return ((func)(vslq->vsl, cp, priv));
	else
		return (0);
}

struct VSLQ *
VSLQ_New(struct VSL_data *vsl, struct VSL_cursor **cp,
    enum VSL_grouping_e grouping, const char *querystring)
{
	struct vslq_query *query;
	struct VSLQ *vslq;

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	AN(cp);
	if (grouping > VSL_g_session) {
		(void)vsl_diag(vsl, "Illegal query grouping");
		return (NULL);
	}
	if (querystring != NULL) {
		query = vslq_newquery(vsl, grouping, querystring);
		if (query == NULL)
			return (NULL);
	} else
		query = NULL;

	ALLOC_OBJ(vslq, VSLQ_MAGIC);
	AN(vslq);
	vslq->vsl = vsl;
	vslq->c = *cp;
	*cp = NULL;
	vslq->grouping = grouping;
	vslq->query = query;
	VRB_INIT(&vslq->tree);
	VTAILQ_INIT(&vslq->incomplete);
	VTAILQ_INIT(&vslq->cache);

	return (vslq);
}

void
VSLQ_Delete(struct VSLQ **pvslq)
{
	struct VSLQ *vslq;
	struct vtx *vtx;

	AN(pvslq);
	vslq = *pvslq;
	*pvslq = NULL;
	CHECK_OBJ_NOTNULL(vslq, VSLQ_MAGIC);

	(void)VSLQ_Flush(vslq, NULL, NULL);
	AZ(vslq->n_incomplete);
	VSL_DeleteCursor(vslq->c);
	vslq->c = NULL;

	if (vslq->query != NULL)
		vslq_deletequery(&vslq->query);
	AZ(vslq->query);

	while (!VTAILQ_EMPTY(&vslq->cache)) {
		AN(vslq->n_cache);
		vtx = VTAILQ_FIRST(&vslq->cache);
		VTAILQ_REMOVE(&vslq->cache, vtx, list_child);
		vslq->n_cache--;
		vtx_free(&vtx);
		AZ(vtx);
	}

	FREE_OBJ(vslq);
}

static int
vslq_raw(struct VSLQ *vslq, VSLQ_dispatch_f *func, void *priv)
{
	struct vslc_raw rawc;
	struct VSL_cursor *c;
	struct VSL_cursor *pc[2];
	int i;

	assert(vslq->grouping == VSL_g_raw);
	c = vslq->c;

	memset(&rawc, 0, sizeof rawc);
	rawc.c.c.vxid = -1;
	rawc.c.magic = VSLC_MAGIC;
	rawc.c.next = vslc_raw_next;
	rawc.c.reset = vslc_raw_reset;
	rawc.magic = VSLC_RAW_MAGIC;
	pc[0] = &rawc.c.c;
	pc[1] = NULL;

	while (1) {
		i = VSL_Next(c);
		if (i <= 0)
			break;
		AN(c->ptr);
		if (func == NULL)
			continue;
		rawc.start = c->ptr;
		rawc.len = VSL_NEXT(c->ptr) - c->ptr;
		rawc.next = rawc.start;
		rawc.c.c.ptr = NULL;

		/* Query check goes here */
		i = 0;
		if (vslq->query == NULL ? 1 : vslq_runquery(vslq->query, pc))
			i = (func)(vslq->vsl, pc, priv);
		if (i)
			break;
	}

	return (i);
}

int
VSLQ_Dispatch(struct VSLQ *vslq, VSLQ_dispatch_f *func, void *priv)
{
	struct VSL_cursor *c;
	int i;
	enum VSL_tag_e tag;
	const uint32_t *ptr;
	ssize_t len;
	unsigned vxid;
	struct vtx *vtx;
	double now;

	CHECK_OBJ_NOTNULL(vslq, VSLQ_MAGIC);

	if (vslq->grouping == VSL_g_raw)
		return (vslq_raw(vslq, func, priv));

	c = vslq->c;
	while (1) {
		i = VSL_Next(c);
		if (i != 1)
			break;
		tag = VSL_TAG(c->ptr);
		if (tag == SLT__Batch) {
			ptr = VSL_NEXT(c->ptr);
			len = VSL_WORDS(c->ptr[1]);
			AZ(vsl_skip(c, len));
		} else {
			ptr = c->ptr;
			len = VSL_NEXT(ptr) - ptr;
		}
		vxid = VSL_ID(ptr);
		if (vxid == 0)
			continue;
		vtx = vtx_lori(vslq, vxid);
		AN(vtx);
		vtx_append(vtx, ptr, len, c->shmptr_ok);
		vtx = vtx_scan(vslq, vtx);
		if (vtx) {
			AN(vtx->flags & VTX_F_READY);
			i = vslq_callback(vslq, vtx, func, priv);
			vtx_retire(vslq, &vtx);
			AZ(vtx);
			if (i)
				break;
		}
	}
	if (i)
		return (i);

	now = VTIM_mono();
	while ((vtx = VTAILQ_FIRST(&vslq->incomplete)) &&
	    now - vtx->t_start > 120.) {
		/* XXX: Make timeout configurable through options and
		   provide a sane default */
		AZ(vtx->flags & VTX_F_COMPLETE);
		vtx = vtx_force(vslq, vtx, "incomplete - timeout");
		if (vtx) {
			AN(vtx->flags & VTX_F_READY);
			i = vslq_callback(vslq, vtx, func, priv);
			vtx_retire(vslq, &vtx);
			AZ(vtx);
			if (i)
				break;
		}
	}
	if (i)
		return (i);

	while (vslq->n_incomplete > 1000) {
		/* XXX: Make limit configurable through options and
		   provide a sane default */
		vtx = VTAILQ_FIRST(&vslq->incomplete);
		AN(vtx);
		AZ(vtx->flags & VTX_F_COMPLETE);
		vtx = vtx_force(vslq, vtx, "incomplete - store overflow");
		if (vtx) {
			AN(vtx->flags & VTX_F_READY);
			i = vslq_callback(vslq, vtx, func, priv);
			vtx_retire(vslq, &vtx);
			AZ(vtx);
			if (i)
				break;
		}
	}

	return (i);
}

int
VSLQ_Flush(struct VSLQ *vslq, VSLQ_dispatch_f *func, void *priv)
{
	struct vtx *vtx;
	int i = 0;

	CHECK_OBJ_NOTNULL(vslq, VSLQ_MAGIC);

	while (vslq->n_incomplete) {
		vtx = VTAILQ_FIRST(&vslq->incomplete);
		AN(vtx);
		AZ(vtx->flags & VTX_F_COMPLETE);
		vtx = vtx_force(vslq, vtx, "incomplete - flushing");
		if (vtx) {
			AN(vtx->flags & VTX_F_READY);
			i = vslq_callback(vslq, vtx, func, priv);
			vtx_retire(vslq, &vtx);
			AZ(vtx);
			if (i)
				break;
		}
	}
	return (i);
}
