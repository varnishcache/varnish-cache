/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef HAVE_STRLCPY
#include "compat/strlcpy.h"
#endif

#include "heritage.h"
#include "mgt.h"

/*--------------------------------------------------------------------*/

void
TCP_name(struct sockaddr *addr, unsigned l, char *abuf, unsigned alen, char *pbuf, unsigned plen)
{
	int i;

	i = getnameinfo(addr, l, abuf, alen, pbuf, plen,
	   NI_NUMERICHOST | NI_NUMERICSERV);
	if (i) {
		printf("getnameinfo = %d %s\n", i, gai_strerror(i));
		strlcpy(abuf, "Conversion", alen);
		strlcpy(pbuf, "Failed", plen);
		return;
	}
	/* XXX dirty hack for v4-to-v6 mapped addresses */
	if (strncmp(abuf, "::ffff:", 7) == 0) {
		for (i = 0; abuf[i + 7]; ++i)
			abuf[i] = abuf[i + 7];
		abuf[i] = '\0';
	}
}

/*--------------------------------------------------------------------*/

void
TCP_myname(int sock, char *abuf, unsigned alen, char *pbuf, unsigned plen)
{
	struct sockaddr_storage addr_s;
	struct sockaddr	*addr = (void*)&addr_s;
	socklen_t l;

	l = sizeof addr;
	AZ(getsockname(sock, addr, &l));
	TCP_name(addr, l, abuf, alen, pbuf, plen);
}

/*--------------------------------------------------------------------*/

int
TCP_filter_http(int sock)
{
#ifdef HAVE_ACCEPT_FILTERS
	struct accept_filter_arg afa;
	int i;

	bzero(&afa, sizeof(afa));
	strcpy(afa.af_name, "httpready");
	errno = 0;
	i = setsockopt(sock, SOL_SOCKET, SO_ACCEPTFILTER,
	    &afa, sizeof(afa));
	/* XXX ugly */
	if (i)
		printf("Acceptfilter(%d, httpready): %d %s\n",
		    sock, i, strerror(errno));
	return (i);
#else
	(void)sock;
	return (0);
#endif
}
