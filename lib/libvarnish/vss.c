/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgrav <des@des.no>
 * Author: Cecilie Fritzvold <cecilihf@linpro.no>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 */

#include "config.h"

#include <sys/socket.h>

#include <netdb.h>
#include <stdlib.h>
#include <string.h>

#include "vdef.h"

#include "vas.h"
#include "vsa.h"
#include "vss.h"

/*lint -esym(754, _storage) not ref */

/*
 * Take a string provided by the user and break it up into address and
 * port parts. The address and port separator may be either a colon or
 * a whitespace. Examples of acceptable input include:
 *
 * "localhost" - "localhost:80" - "localhost 80"
 * "127.0.0.1" - "127.0.0.1:80" - "127.0.0.1 80"
 * "0.0.0.0"   - "0.0.0.0:80"   - "0.0.0.0 80"
 * "[::1]"     - "[::1]:80"     - "[::1] 80"
 * "[::]"      - "[::]:80"      - "[::] 80"
 * "::1"       - "[::1]:80"     - "[::1] 80"
 *
 * See also RFC5952
 */

static const char *
vss_parse(char *str, char **addr, char **port)
{
	char *p;

	*addr = *port = NULL;

	if (str[0] == '[') {
		/* IPv6 address of the form [::1]:80 */
		*addr = str + 1;
		p = strchr(str, ']');
		if (p == NULL)
			return ("IPv6 address lacks ']'");
		*p++ = '\0';
		if (*p == '\0')
			return (NULL);
		if (*p != ' ' && *p != ':')
			return ("IPv6 address has wrong port separator");
	} else {
		/*
		 * IPv4 address of the form 127.0.0.1:80, IPv6 address
		 * without port or non-numeric.
		 */
		*addr = str;
		p = strchr(str, ' ');
		if (p == NULL)
			p = strchr(str, ':');
		if (p == NULL)
			return (NULL);
		if (p[0] == ':' && strchr(&p[1], ':'))
			return (NULL);
		if (p == str)
			*addr = NULL;
	}
	*p++ = '\0';
	*port = p;
	return (NULL);
}

static int
vss_resolve(const char *addr, const char *def_port, int family, int socktype,
    int flags, struct addrinfo **res, const char **errp)
{
	struct addrinfo hints;
	char *p, *hp, *pp;
	int ret;

	AN(addr);
	AN(res);
	AZ(*res);
	AN(errp);
	*errp = NULL;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = family;
	hints.ai_socktype = socktype;
	hints.ai_flags = flags;

	p = strdup(addr);
	AN(p);
	*errp = vss_parse(p, &hp, &pp);
	if (*errp != NULL) {
		free(p);
		return (-1);
	}
	if (pp != NULL)
		def_port = pp;
	ret = getaddrinfo(hp, def_port, &hints, res);
	free(p);

	if (ret == EAI_SYSTEM)
		*errp = VAS_errtxt(errno);
	else if (ret != 0)
		*errp = gai_strerror(ret);

	return (ret);
}

static struct suckaddr *
vss_alloc_suckaddr(void *dst, const struct addrinfo *ai)
{

	AN(ai);
	if (dst == NULL)
		return (VSA_Malloc(ai->ai_addr, ai->ai_addrlen));

	return (VSA_Build(dst, ai->ai_addr, ai->ai_addrlen));
}

/*
 * Look up an address, using a default port if provided, and call
 * the callback function with the suckaddrs we find.
 * If the callback function returns anything but zero, we terminate
 * and pass that value.
 */

int
VSS_resolver_socktype(const char *addr, const char *def_port,
    vss_resolved_f *func, void *priv, const char **errp, int socktype)
{
	struct addrinfo *res0 = NULL, *res;
	struct suckaddr *vsa;
	int ret;

	AN(addr);
	AN(func);
	AN(errp);

	ret = vss_resolve(addr, def_port, AF_UNSPEC, socktype, AI_PASSIVE,
	    &res0, errp);
	if (ret != 0)
		return (-1);

	for (res = res0; res != NULL; res = res->ai_next) {
		vsa = VSA_Malloc(res->ai_addr, res->ai_addrlen);
		if (vsa != NULL) {
			ret = func(priv, vsa);
			free(vsa);
			if (ret)
				break;
		}
	}
	freeaddrinfo(res0);
	return (ret);
}

int
VSS_resolver(const char *addr, const char *def_port, vss_resolved_f *func,
    void *priv, const char **errp)
{
	return (VSS_resolver_socktype(
	    addr, def_port, func, priv, errp, SOCK_STREAM));
}

struct suckaddr *
VSS_ResolveOne(void *dst, const char *addr, const char *def_port,
    int family, int socktype, int flags)
{
	struct addrinfo *res = NULL;
	struct suckaddr *retval = NULL;
	const char *err;
	int ret;

	AN(addr);
	ret = vss_resolve(addr, def_port, family, socktype, flags, &res, &err);
	if (ret == 0 && res != NULL && res->ai_next == NULL) {
		AZ(err);
		retval = vss_alloc_suckaddr(dst, res);
	}
	if (res != NULL)
		freeaddrinfo(res);
	return (retval);
}

struct suckaddr *
VSS_ResolveFirst(void *dst, const char *addr, const char *def_port,
    int family, int socktype, int flags)
{
	struct addrinfo *res0 = NULL, *res;
	struct suckaddr *retval = NULL;
	const char *err;
	int ret;

	AN(addr);
	ret = vss_resolve(addr, def_port, family, socktype, flags, &res0, &err);
	if (ret == 0)
		AZ(err);

	for (res = res0; res != NULL; res = res->ai_next) {
		retval = vss_alloc_suckaddr(dst, res);
		if (retval != NULL)
			break;
	}
	if (res0 != NULL)
		freeaddrinfo(res0);
	return (retval);
}
