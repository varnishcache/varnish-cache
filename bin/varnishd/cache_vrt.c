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
#include "vcl_lang.h"
#include "vrt.h"
#include "libvarnish.h"
#include "cache.h"

/*--------------------------------------------------------------------*/

void
VRT_error(VCL_FARGS, unsigned err, const char *str)
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
VRT_GetHdr(VCL_FARGS, const char *n)
{
	char *p;

	assert(sess != NULL);
	assert(sess->http != NULL);
	if (!http_GetHdr(sess->http, n, &p))
		return (NULL);
	return (p);
}

/*--------------------------------------------------------------------*/

char *
VRT_GetReq(VCL_FARGS)
{
	char *p;

	assert(sess != NULL);
	assert(sess->http != NULL);
	assert(http_GetReq(sess->http, &p));
	return (p);
}
