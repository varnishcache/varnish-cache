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
VRT_handling(struct sess *sp, enum handling hand)
{

	sp->handling = hand;
}
