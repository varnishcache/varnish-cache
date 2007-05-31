/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Linpro AS
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
#ifndef HAVE_STRNDUP
#include "compat/strndup.h"
#endif

#include "heritage.h"
#include "mgt.h"
#include "cli.h"
#include "cli_priv.h"

/* lightweight addrinfo */
struct tcp_addr {
	int			 ta_family;
	int			 ta_socktype;
	int			 ta_protocol;
	socklen_t		 ta_addrlen;
	struct sockaddr_storage	 ta_addr;
};

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
	struct sockaddr addr[2];	/* XXX: IPv6 hack */
	socklen_t l;

	l = sizeof addr;
	AZ(getsockname(sock, addr, &l));
	TCP_name(addr, l, abuf, alen, pbuf, plen);
}

/*--------------------------------------------------------------------*/

#ifdef HAVE_ACCEPT_FILTERS
static void
accept_filter(int fd)
{
	struct accept_filter_arg afa;
	int i;

	bzero(&afa, sizeof(afa));
	strcpy(afa.af_name, "httpready");
	errno = 0;
	i = setsockopt(fd, SOL_SOCKET, SO_ACCEPTFILTER,
	    &afa, sizeof(afa));
	if (i)
		printf("Acceptfilter(%d, httpready): %d %s\n",
		    fd, i, strerror(errno));
}
#endif

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
TCP_parse(const char *str, char **addr, char **port)
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
 * For a given host and port, return a list of struct tcp_addr, which
 * contains all the information necessary to open and bind a socket.  One
 * tcp_addr is returned for each distinct address returned by
 * getaddrinfo().
 *
 * The value pointed to by the tap parameter receives a pointer to an
 * array of pointers to struct tcp_addr.  The caller is responsible for
 * freeing each individual struct tcp_addr as well as the array.
 *
 * The return value is the number of addresses resoved, or zero.
 */
int
TCP_resolve(const char *addr, const char *port, struct tcp_addr ***tap)
{
	struct addrinfo hints, *res0, *res;
	struct tcp_addr **ta;
	int i, ret;

        memset(&hints, 0, sizeof hints);
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        ret = getaddrinfo(addr, port, &hints, &res0);
        if (ret != 0) {
                fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(ret));
                return (0);
        }
	for (res = res0, i = 0; res != NULL; res = res->ai_next)
		++i;
	ta = calloc(i, sizeof *ta);
	XXXAN(ta);
	*tap = ta;
	for (res = res0, i = 0; res != NULL; res = res->ai_next, ++i) {
		ta[i] = calloc(1, sizeof *ta[i]);
		XXXAN(ta[i]);
		ta[i]->ta_family = res->ai_family;
		ta[i]->ta_socktype = res->ai_socktype;
		ta[i]->ta_protocol = res->ai_protocol;
		ta[i]->ta_addrlen = res->ai_addrlen;
		xxxassert(ta[i]->ta_addrlen <= sizeof ta[i]->ta_addr);
		memcpy(&ta[i]->ta_addr, res->ai_addr, ta[i]->ta_addrlen);
	}
	freeaddrinfo(res0);
	return (i);
}

/*
 * Given a struct tcp_addr, open a socket of the appropriate type, bind it
 * to the requested address, and start listening.
 *
 * If the address is an IPv6 address, the IPV6_V6ONLY option is set to
 * avoid conflicts between INADDR_ANY and IN6ADDR_ANY.
 *
 * If the http parameter is non-zero and accept filters are available,
 * install an HTTP accept filter on the socket.
 */
int
TCP_open(const struct tcp_addr *ta, int http)
{
	int sd, val;

	sd = socket(ta->ta_family, ta->ta_socktype, ta->ta_protocol);
	if (sd < 0) {
		perror("socket()");
		return (-1);
	}
	val = 1;
	if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) != 0) {
		perror("setsockopt(SO_REUSEADDR, 1)");
		close(sd);
		return (-1);
	}
#ifdef IPV6_V6ONLY
	/* forcibly use separate sockets for IPv4 and IPv6 */
	val = 1;
	if (ta->ta_family == AF_INET6 &&
	    setsockopt(sd, IPPROTO_IPV6, IPV6_V6ONLY, &val, sizeof val) != 0) {
		perror("setsockopt(IPV6_V6ONLY, 1)");
		close(sd);
		return (-1);
	}
#endif
	if (bind(sd, (const struct sockaddr *)&ta->ta_addr, ta->ta_addrlen) != 0) {
		perror("bind()");
		close(sd);
		return (-1);
	}
	if (listen(sd, http ? params->listen_depth : 16) != 0) {
		perror("listen()");
		close(sd);
		return (-1);
	}
#ifdef HAVE_ACCEPT_FILTERS
	if (http)
		accept_filter(sd);
#endif
	return (sd);
}
