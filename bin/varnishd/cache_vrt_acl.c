/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * $Id$
 *
 * Runtime support for compiled VCL programs, ACLs
 *
 * XXX: getaddrinfo() does not return a TTL.  We might want to add
 * XXX: a refresh facility.
 */

#include "config.h"

#include <sys/types.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "shmlog.h"
#include "vrt.h"
#include "vcl.h"
#include "cache.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>


static uint32_t ipv4mask[] = {
	[0]	=	0xffffffff,
#define M(n)	[n] = (uint32_t)((uint64_t)0xffffffff << (32 - n))
	M( 1), M( 2), M( 3), M( 4), M( 5), M( 6), M( 7), M( 8), M( 9), M(10),
	M(11), M(12), M(13), M(14), M(15), M(16), M(17), M(18), M(19), M(20),
	M(21), M(22), M(23), M(24), M(25), M(26), M(27), M(28), M(29), M(30),
	M(31), M(32)
};

static int
vrt_acl_vsl(struct sess *sp, const char *acln, struct vrt_acl *ap, int r)
{

	AN(ap);
	if (acln != NULL) {
		if (ap->name == NULL) {
			assert(r == 0);
			VSL(SLT_VCL_acl, sp->fd, "NO_MATCH %s", acln);
			return (r);
		}
		if (ap->priv == NULL) {
			assert(r == 0);
			VSL(SLT_VCL_acl, sp->fd, "FAIL %s %s", acln, ap->desc);
			return (r);
		}

		VSL(SLT_VCL_acl, sp->fd, "%s %s %s",
			r ? "MATCH" : "NEG_MATCH", acln, ap->desc);
	}
	return (r);
}

int
VRT_acl_match(struct sess *sp, struct sockaddr *sa, const char *acln, struct vrt_acl *ap)
{
	struct addrinfo *a1;
	struct sockaddr_in *sin1, *sin2;

	if (sa->sa_family == AF_INET)
		sin1 = (void*)sa;
	else
		sin1 = NULL;

	for ( ; ap->name != NULL; ap++) {
		if (ap->priv == NULL && ap->paren)
			continue;
		if (ap->priv == NULL && ap->not) {
			return (vrt_acl_vsl(sp, acln, ap, 0));
		}
		if (ap->priv == NULL)
			continue;
		for (a1 = ap->priv; a1 != NULL; a1 = a1->ai_next) {

			/* only match the right family */
			if (a1->ai_family != sp->sockaddr->sa_family)
				continue;

			if (a1->ai_family == AF_INET) {
				assert(sin1 != NULL);
				assert(a1->ai_addrlen >= sizeof (*sin2));
				sin2 = (void*)a1->ai_addr;
				if (0 == ((
				    htonl(sin1->sin_addr.s_addr) ^
				    htonl(sin2->sin_addr.s_addr)) &
				    ipv4mask[ap->mask > 32 ? 32 : ap->mask]))
					return (
					    vrt_acl_vsl(sp, acln, ap, !ap->not));
				continue;
			}

			/* Not rules for unknown protos match */
			if (ap->not)
				return (vrt_acl_vsl(sp, acln, ap, 0));
		}
	}
	return (vrt_acl_vsl(sp, acln, ap, 0));
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
