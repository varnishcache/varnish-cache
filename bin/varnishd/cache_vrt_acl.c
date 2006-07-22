/*
 * $Id$
 *
 * Runtime support for compiled VCL programs, ACLs
 *
 * XXX: getaddrinfo() does not return a TTL.  We might want to add
 * XXX: a refresh facility.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "shmlog.h"
#include "vrt.h"
#include "vrt_obj.h"
#include "vcl.h"
#include "cache.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>


int
VRT_acl_match(struct sess *sp, struct vrt_acl *ap)
{
	(void)sp;
	(void)ap;
	return (0);
}

void
VRT_acl_init(struct vrt_acl *ap)
{
	struct addrinfo a0, *a1;
	int i;

	memset(&a0, 0, sizeof a0);
	a0.ai_socktype = SOCK_STREAM;

	for ( ; ap->name != NULL; ap++) {
		a1 = NULL;
		i = getaddrinfo(ap->name, NULL, &a0, &a1);
		if (i != 0) {
			fprintf(stderr, "getaddrinfo(%s) = %s\n",
			    ap->name, gai_strerror(i));
			if (a1 != NULL) 
				freeaddrinfo(a1);
			a1 = NULL;
		}
		ap->priv = a1;
	}
}

void
VRT_acl_fini(struct vrt_acl *ap)
{
	struct addrinfo *a1;

	for ( ; ap->name != NULL; ap++) {
		if (ap->priv == NULL)
			continue;
		a1 = ap->priv;
		ap->priv = NULL;
		freeaddrinfo(a1);
	}
}


