/*-
 * Copyright (c) 2013 Varnish Software AS
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
 * Struct sockaddr_* is not even close to a convenient API.
 *
 * These functions try to mitigate the madness, at the cost of actually
 * knowing something about address families.
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "vas.h"
#include "vsa.h"

int
VSA_Sane(const struct sockaddr_storage *ss)
{
	switch(ss->ss_family) {
		case PF_INET:
		case PF_INET6:
			return (1);
		default:
			return (0);
	}
}

socklen_t
VSA_Len(const struct sockaddr_storage *ss)
{
	switch(ss->ss_family) {
		case PF_INET:
			return (sizeof(struct sockaddr_in));
		case PF_INET6:
			return (sizeof(struct sockaddr_in6));
		default:
			WRONG("Illegal socket family");
	}
}

int
VSA_Compare(const struct sockaddr_storage *ss1, const void *ss2)
{
	switch(ss1->ss_family) {
		case PF_INET:
		case PF_INET6:
			return (memcmp(ss1, ss2, VSA_Len(ss1)));
		default:
			return (-1);
	}
}

unsigned
VSA_Port(const struct sockaddr_storage *ss)
{
	switch(ss->ss_family) {
		case PF_INET:
			{
			const struct sockaddr_in *ain = (const void *)ss;
			return (ntohs((ain->sin_port)));
			}
		case PF_INET6:
			{
			const struct sockaddr_in6 *ain = (const void *)ss;
			return (ntohs((ain->sin6_port)));
			}
		default:
			WRONG("Illegal socket family");
	}
}
