/*
 * $Id$
 *
 * Runtime support for compiled VCL programs
 */


#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/queue.h>

#include "cli.h"
#include "cli_priv.h"
#include "shmlog.h"
#include "vrt.h"
#include "libvarnish.h"
#include "vcl_lang.h"
#include "cache.h"

/*--------------------------------------------------------------------*/

void
VRT_error(struct sess *sp, unsigned err, const char *str)
{ 

	VSL(SLT_Debug, 0, "VCL_error(%u, %s)", err, str);
}

/*--------------------------------------------------------------------*/

void
VRT_count(struct sess *sp, unsigned u)
{
	
	VSL(SLT_VCL, 0, "%u %d.%d", u,
	    sp->vcl->ref[u].line,
	    sp->vcl->ref[u].pos);
}

/*--------------------------------------------------------------------*/

char *
VRT_GetHdr(struct sess *sp, const char *n)
{
	char *p;

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
	char *p;

	assert(sp != NULL);
	assert(sp->http != NULL);
	assert(http_GetReq(sp->http, &p));
	return (p);
}

/*--------------------------------------------------------------------*/

void
VRT_handling(struct sess *sp, unsigned hand)
{

	assert(!(hand & (hand -1)));	/* must be power of two */
	switch (hand) {
#define FOO(a,b)  case VRT_H_##a: sp->handling = HND_##b; break;
	FOO(error, Error);
	FOO(pipe, Pipe);
	FOO(pass, Pass);
	FOO(lookup, Lookup);
	FOO(fetch, Fetch);
	FOO(insert, Insert);
	FOO(deliver, Deliver);
#undef FOO
	default:
		assert(hand == 0);
	}
}

int
VRT_obj_valid(struct sess *sp)
{
	return (sp->obj->valid);
}

int
VRT_obj_cacheable(struct sess *sp)
{
	return (sp->obj->cacheable);
}

void
VRT_set_backend_hostname(struct backend *be, const char *h)
{
	be->hostname = h;
}

void
VRT_set_backend_portname(struct backend *be, const char *p)
{
	be->portname = p;
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
	}
}
