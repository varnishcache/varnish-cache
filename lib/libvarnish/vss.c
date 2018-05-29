/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgrav <des@des.no>
 * Author: Cecilie Fritzvold <cecilihf@linpro.no>
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
 * port parts.  Examples of acceptable input include:
 *
 * "localhost" - "localhost:80"
 * "127.0.0.1" - "127.0.0.1:80"
 * "0.0.0.0" - "0.0.0.0:80"
 * "[::1]" - "[::1]:80"
 * "[::]" - "[::]:80"
 * "::1" - "[::1]:80"
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

/*
 * Look up an address, using a default port if provided, and call
 * the callback function with the suckaddrs we find.
 * If the callback function returns anything but zero, we terminate
 * and pass that value.
 */

int
VSS_resolver_socktype(const char *addr, const char *def_port,
    vss_resolved_f *func, void *priv, const char **err, int socktype)
{
	struct addrinfo hints, *res0, *res;
	struct suckaddr *vsa;
	char *h;
	char *adp, *hop;
	int ret;

	*err = NULL;
	h = strdup(addr);
	AN(h);
	*err = vss_parse(h, &hop, &adp);
	if (*err != NULL) {
		free(h);
		return (-1);
	}
	if (adp != NULL)
		def_port = adp;

	memset(&hints, 0, sizeof hints);
	hints.ai_socktype = socktype;
	hints.ai_flags = AI_PASSIVE;
	ret = getaddrinfo(hop, def_port, &hints, &res0);
	free(h);
	if (ret != 0) {
		*err = gai_strerror(ret);
		return (-1);
	}
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
    void *priv, const char **err)
{
	return (VSS_resolver_socktype(
	    addr, def_port, func, priv, err, SOCK_STREAM));
}
