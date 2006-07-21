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

char *
VRT_GetReq(struct sess *sp)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(sp != NULL);
	assert(sp->http != NULL);
	return (sp->http->hd[HTTP_HDR_REQ].b);
}

/*--------------------------------------------------------------------*/

void
VRT_handling(struct sess *sp, unsigned hand)
{

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	assert(!(hand & (hand -1)));	/* must be power of two */
	sp->handling = hand;
}

int
VRT_obj_valid(struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (sp->obj->valid);
}

int
VRT_obj_cacheable(struct sess *sp)
{
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	return (sp->obj->cacheable);
}

void
VRT_set_backend_hostname(struct backend *be, const char *h)
{
	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
	be->hostname = h;
}

void
VRT_set_backend_portname(struct backend *be, const char *p)
{
	CHECK_OBJ_NOTNULL(be, BACKEND_MAGIC);
	be->portname = p;
}

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
	}
}
