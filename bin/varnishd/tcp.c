/*
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

#include "mgt.h"
#include "cli.h"
#include "cli_priv.h"

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
		if (p[1] == ':')
			*port = strdup(p + 2);
	} else {
		/* IPv4 address of the form 127.0.0.1:80, or non-numeric */
		p = strchr(str, ':');
		if (p == NULL) {
			*addr = strdup(str);
		} else {
			if (p > str)
				*addr = strndup(str, p - str);
			*port = strdup(p + 1);
		}
	}
	return (0);
}

/*--------------------------------------------------------------------*/

void
TCP_check(struct cli *cli, const char *addr, const char *port)
{
	struct addrinfo hints, *res;
	int ret;

	memset(&hints, 0, sizeof hints);
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	ret = getaddrinfo(addr, port, &hints, &res);
	if (ret == 0) {
		freeaddrinfo(res);
		return;
	}
	cli_out(cli, "getaddrinfo(%s, %s): %s\n",
	    addr, port, gai_strerror(ret));
	cli_result(cli, CLIS_PARAM);
}

int
TCP_open(const char *addr, const char *port, int http)
{
	struct addrinfo hints, *res;
	int ret, sd, val;

	memset(&hints, 0, sizeof hints);
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	ret = getaddrinfo(addr, port, &hints, &res);
	if (ret != 0) {
		fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(ret));
		return (-1);
	}
	sd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sd < 0) {
		perror("socket()");
		freeaddrinfo(res);
		return (-1);
	}
	val = 1;
	if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) != 0) {
		perror("setsockopt(SO_REUSEADDR, 1)");
		freeaddrinfo(res);
		close(sd);
		return (-1);
	}
	if (bind(sd, res->ai_addr, res->ai_addrlen) != 0) {
		perror("bind()");
		freeaddrinfo(res);
		close(sd);
		return (-1);
	}
	if (listen(sd, http ? 1024 : 16) != 0) {
		perror("listen()");
		freeaddrinfo(res);
		close(sd);
		return (-1);
	}
#ifdef HAVE_ACCEPT_FILTERS
	if (http)
		accept_filter(sd);
#else
	(void)http;
#endif
	freeaddrinfo(res);
	return (sd);
}
