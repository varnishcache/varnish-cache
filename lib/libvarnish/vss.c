/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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

#include "svnid.h"
SVNID("$Id$")

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
#ifndef HAVE_STRNDUP
#include "compat/strndup.h"
#endif

#include "libvarnish.h"
#include "vss.h"

/* lightweight addrinfo */
struct vss_addr {
	int			 va_family;
	int			 va_socktype;
	int			 va_protocol;
	socklen_t		 va_addrlen;
	union {
		struct sockaddr_storage	 _storage;
		struct sockaddr		 sa;
	} va_addr;
};

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
 */
int
VSS_parse(const char *str, char **addr, char **port)
{
	const char *p;

	*addr = *port = NULL;

	if (str[0] == '[') {
		/* IPv6 address of the form [::1]:80 */
		if ((p = strchr(str, ']')) == NULL ||
		    p == str + 1 ||
		    (p[1] != '\0' && p[1] != ':'))
			return (-1);
		*addr = strndup(str + 1, p - (str + 1));
		XXXAN(*addr);
		if (p[1] == ':') {
			*port = strdup(p + 2);
			XXXAN(*port);
		}
	} else {
		/* IPv4 address of the form 127.0.0.1:80, or non-numeric */
		p = strchr(str, ':');
		if (p == NULL) {
			*addr = strdup(str);
			XXXAN(*addr);
		} else {
			if (p > str) {
				*addr = strndup(str, p - str);
				XXXAN(*addr);
			}
			*port = strdup(p + 1);
			XXXAN(*port);
		}
	}
	return (0);
}

/*
 * For a given host and port, return a list of struct vss_addr, which
 * contains all the information necessary to open and bind a socket.  One
 * vss_addr is returned for each distinct address returned by
 * getaddrinfo().
 *
 * The value pointed to by the tap parameter receives a pointer to an
 * array of pointers to struct vss_addr.  The caller is responsible for
 * freeing each individual struct vss_addr as well as the array.
 *
 * The return value is the number of addresses resoved, or zero.
 *
 * XXX: We need a function to free the allocated addresses.
 */
int
VSS_resolve(const char *addr, const char *port, struct vss_addr ***vap)
{
	struct addrinfo hints, *res0, *res;
	struct vss_addr **va;
	int i, ret;

	memset(&hints, 0, sizeof hints);
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	ret = getaddrinfo(addr, port, &hints, &res0);
	if (ret != 0) {
		fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(ret));
		return (0);
	}
	XXXAN(res0);
	for (res = res0, i = 0; res != NULL; res = res->ai_next, ++i)
		/* nothing */ ;
	if (i == 0) {
		freeaddrinfo(res0);
		return (0);
	}
	va = calloc(i, sizeof *va);
	XXXAN(va);
	*vap = va;
	for (res = res0, i = 0; res != NULL; res = res->ai_next, ++i) {
		va[i] = calloc(1, sizeof(**va));
		XXXAN(va[i]);
		va[i]->va_family = res->ai_family;
		va[i]->va_socktype = res->ai_socktype;
		va[i]->va_protocol = res->ai_protocol;
		va[i]->va_addrlen = res->ai_addrlen;
		xxxassert(va[i]->va_addrlen <= sizeof va[i]->va_addr);
		memcpy(&va[i]->va_addr, res->ai_addr, va[i]->va_addrlen);
	}
	freeaddrinfo(res0);
	return (i);
}

/*
 * Given a struct vss_addr, open a socket of the appropriate type, and bind
 * it to the requested address.
 *
 * If the address is an IPv6 address, the IPV6_V6ONLY option is set to
 * avoid conflicts between INADDR_ANY and IN6ADDR_ANY.
 */

int
VSS_bind(const struct vss_addr *va)
{
	int sd, val;

	sd = socket(va->va_family, va->va_socktype, va->va_protocol);
	if (sd < 0) {
		perror("socket()");
		return (-1);
	}
	val = 1;
	if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) != 0) {
		perror("setsockopt(SO_REUSEADDR, 1)");
		(void)close(sd);
		return (-1);
	}
#ifdef IPV6_V6ONLY
	/* forcibly use separate sockets for IPv4 and IPv6 */
	val = 1;
	if (va->va_family == AF_INET6 &&
	    setsockopt(sd, IPPROTO_IPV6, IPV6_V6ONLY, &val, sizeof val) != 0) {
		perror("setsockopt(IPV6_V6ONLY, 1)");
		(void)close(sd);
		return (-1);
	}
#endif
	if (bind(sd, &va->va_addr.sa, va->va_addrlen) != 0) {
		perror("bind()");
		(void)close(sd);
		return (-1);
	}
	return (sd);
}

/*
 * Given a struct vss_addr, open a socket of the appropriate type, bind it
 * to the requested address, and start listening.
 *
 * If the address is an IPv6 address, the IPV6_V6ONLY option is set to
 * avoid conflicts between INADDR_ANY and IN6ADDR_ANY.
 */
int
VSS_listen(const struct vss_addr *va, int depth)
{
	int sd;

	sd = VSS_bind(va);
	if (sd >= 0)  {
		if (listen(sd, depth) != 0) {
			perror("listen()");
			(void)close(sd);
			return (-1);
		}
	}
	return (sd);
}

/*
 * Connect to the socket specified by the address info in va.
 * Return the socket.
 */
int
VSS_connect(const struct vss_addr *va, int nonblock)
{
	int sd, i;

	sd = socket(va->va_family, va->va_socktype, va->va_protocol);
	if (sd < 0) {
		if (errno != EPROTONOSUPPORT)
			perror("socket()");
		return (-1);
	}
	if (nonblock)
		(void)TCP_nonblocking(sd);
	i = connect(sd, &va->va_addr.sa, va->va_addrlen);
	if (i == 0 || (nonblock && errno == EINPROGRESS))
		return (sd);
	perror("connect()");
	(void)close(sd);
	return (-1);
}

/*
 * And the totally brutal version: Give me connection to this address
 */

int
VSS_open(const char *str)
{
	int retval;
	char *addr = NULL, *port = NULL;
	int nvaddr, n;
	struct vss_addr **vaddr;

	retval = VSS_parse(str, &addr, &port);
	if (retval < 0)
		return (retval);
	nvaddr = VSS_resolve(addr, port, &vaddr);
	if (nvaddr <= 0) {
		free(addr);
		free(port);
		return (-1);
	}
	for (n = 0; n < nvaddr; n++) {
		retval = VSS_connect(vaddr[n], 0);
		if (retval >= 0)
			break;
	}
	for (n = 0; n < nvaddr; n++)
		free(vaddr[n]);
	free(vaddr);
	free(addr);
	free(port);
	return (retval);
}
