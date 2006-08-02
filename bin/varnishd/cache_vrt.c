/*
 * $Id$
 *
 * Runtime support for compiled VCL programs
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "shmlog.h"
#include "vrt.h"
#include "vrt_obj.h"
#include "vcl.h"
#include "cache.h"

/*--------------------------------------------------------------------*/

void
VRT_error(struct sess *sp, unsigned err, const char *str)
{ 

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	VSL(SLT_Debug, 0, "VCL_error(%u, %s)", err, str);
}

/*--------------------------------------------------------------------*/

void
VRT_count(struct sess *sp, unsigned u)
{
	
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	VSL(SLT_VCL_trace, sp->fd, "%u %d.%d", u,
	    sp->vcl->ref[u].line,
	    sp->vcl->ref[u].pos);
}

/*--------------------------------------------------------------------*/

char *
VRT_GetHdr(struct sess *sp, const char *n)
{
	char *p;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(sp != NULL);
	assert(sp->http != NULL);
	if (!http_GetHdr(sp->http, n, &p))
		return (NULL);
	return (p);
}

/*--------------------------------------------------------------------*/

void
VRT_handling(struct sess *sp, unsigned hand)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(!(hand & (hand -1)));	/* must be power of two */
	sp->handling = hand;
}

/*--------------------------------------------------------------------*/

void
VRT_set_backend_name(struct backend *be, const char *p)
{
	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
	be->vcl_name = p;
}

void
VRT_alloc_backends(struct VCL_conf *cp)
{
	int i;

	cp->backend = calloc(sizeof *cp->backend, cp->nbackend);
	assert(cp->backend != NULL);
	for (i = 0; i < cp->nbackend; i++) {
		cp->backend[i] = calloc(sizeof *cp->backend[i], 1);
		assert(cp->backend[i] != NULL);
		cp->backend[i]->magic = BACKEND_MAGIC;
		TAILQ_INIT(&cp->backend[i]->connlist);
	}
}

void
VRT_free_backends(struct VCL_conf *cp)
{

	(void)cp;
}

void
VRT_fini_backend(struct backend *be)
{
	(void)be;
}

/*--------------------------------------------------------------------*/

#define VBACKEND(type,onm,field)			\
void							\
VRT_l_backend_##onm(struct backend *be, type a)		\
{							\
	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);		\
	be->field = a;					\
}							\
							\
type							\
VRT_r_backend_##onm(struct backend *be)			\
{							\
	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);		\
	return (be->field);				\
}

VBACKEND(const char *,	host,	hostname)
VBACKEND(const char *,	port,	portname)

/*--------------------------------------------------------------------
 * XXX: Working relative to t_req is maybe not the right thing, we could
 * XXX: have spent a long time talking to the backend since then.
 * XXX: It might make sense to cache a timestamp as "current time"
 * XXX: before vcl_recv (== t_req) and vcl_fetch.
 * XXX: On the other hand, that might lead to inconsistent behaviour
 * XXX: where an object expires while we are running VCL code, and
 * XXX: and that may not be a good idea either.
 * XXX: See also related t_req use in cache_hash.c
 */

void
VRT_l_obj_ttl(struct sess *sp, double a)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);	/* XXX */
	VSL(SLT_TTL, sp->fd, "%u VCL %.0f %u",
	    sp->obj->xid, a, sp->t_req.tv_sec);
	if (a < 0)
		a = 0;
	sp->obj->ttl = sp->t_req.tv_sec + (int)a;
	if (sp->obj->heap_idx != 0)
		EXP_TTLchange(sp->obj);
}

double
VRT_r_obj_ttl(struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);	/* XXX */
	return (sp->obj->ttl - sp->t_req.tv_sec);
}

/*--------------------------------------------------------------------*/

#define VOBJ(type,onm,field)						\
void									\
VRT_l_obj_##onm(struct sess *sp, type a)				\
{									\
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);				\
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);	/* XXX */	\
	sp->obj->field = a;						\
}									\
									\
type									\
VRT_r_obj_##onm(struct sess *sp)					\
{									\
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);				\
	CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);	/* XXX */	\
	return (sp->obj->field);					\
}

VOBJ(double, valid, valid)
VOBJ(double, cacheable, cacheable)

/*--------------------------------------------------------------------*/

#define  VREQ(n1, n2)					\
const char *						\
VRT_r_req_##n1(struct sess *sp)				\
{							\
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);		\
	CHECK_OBJ_NOTNULL(sp->http, HTTP_MAGIC);	\
	return (sp->http->hd[n2].b);			\
}

VREQ(request, HTTP_HDR_REQ)
VREQ(url, HTTP_HDR_URL)
VREQ(proto, HTTP_HDR_PROTO)
