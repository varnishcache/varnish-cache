/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#ifdef __linux
#include <netinet/tcp.h>
#endif

#include <errno.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "config.h"
#ifndef HAVE_STRLCPY
#include "compat/strlcpy.h"
#endif

#include "libvarnish.h"

/*--------------------------------------------------------------------*/

int
TCP_port(const struct sockaddr_storage *addr)
{

	if (addr->ss_family == AF_INET) {
		const struct sockaddr_in *ain = (const void *)addr;
		return ntohs((ain->sin_port));
	}
	if (addr->ss_family == AF_INET6) {
		const struct sockaddr_in6 *ain = (const void *)addr;
		return ntohs((ain->sin6_port));
	}
	return (-1);
}


/*--------------------------------------------------------------------*/

void
TCP_name(const struct sockaddr_storage *addr, unsigned l,
    char *abuf, unsigned alen, char *pbuf, unsigned plen)
{
	int i;

	i = getnameinfo((const void *)addr, l, abuf, alen, pbuf, plen,
	   NI_NUMERICHOST | NI_NUMERICSERV);
	if (i) {
		/*
		 * XXX this printf is shitty, but we may not have space
		 * for the gai_strerror in the bufffer :-(
		 */
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
	socklen_t l;

	l = sizeof addr_s;
	AZ(getsockname(sock, (void *)&addr_s, &l));
	TCP_name(&addr_s, l, abuf, alen, pbuf, plen);
}
/*--------------------------------------------------------------------*/

void
TCP_hisname(int sock, char *abuf, unsigned alen, char *pbuf, unsigned plen)
{
	struct sockaddr_storage addr_s;
	socklen_t l;

	l = sizeof addr_s;
	if (!getpeername(sock, (void*)&addr_s, &l))
		TCP_name(&addr_s, l, abuf, alen, pbuf, plen);
	else {
		strlcpy(abuf, "<none>", alen);
		strlcpy(pbuf, "<none>", plen);
	}
}

/*--------------------------------------------------------------------*/

int
TCP_filter_http(int sock)
{
#ifdef HAVE_ACCEPT_FILTERS
	struct accept_filter_arg afa;
	int i;

	memset(&afa, 0, sizeof(afa));
	strcpy(afa.af_name, "httpready");
	errno = 0;
	i = setsockopt(sock, SOL_SOCKET, SO_ACCEPTFILTER,
	    &afa, sizeof(afa));
	/* XXX ugly */
	if (i)
		printf("Acceptfilter(%d, httpready): %d %s\n",
		    sock, i, strerror(errno));
	return (i);
#elif defined(__linux)
	int defer = 1;
	setsockopt(sock, SOL_TCP,TCP_DEFER_ACCEPT,(char *) &defer, sizeof(int));
	return (0);
#else
	(void)sock;
	return (0);
#endif
}

/*--------------------------------------------------------------------
 * Functions for controlling NONBLOCK mode.
 *
 * We use FIONBIO because it is cheaper than fcntl(2), which requires
 * us to do two syscalls, one to get and one to set, the latter of
 * which mucks about a bit before it ends up calling ioctl(FIONBIO),
 * at least on FreeBSD.
 */

int
TCP_blocking(int sock)
{
	int i, j;

	i = 0;
	j = ioctl(sock, FIONBIO, &i);
	TCP_Assert(j);
	return (j);
}

int
TCP_nonblocking(int sock)
{
	int i, j;

	i = 1;
	j = ioctl(sock, FIONBIO, &i);
	TCP_Assert(j);
	return (j);
}

/*--------------------------------------------------------------------
 * On TCP a connect(2) can block for a looong time, and we don't want that.
 * Unfortunately, the SocketWizards back in those days were happy to wait
 * any amount of time for a connection, so the connect(2) syscall does not
 * take an argument for patience.
 *
 * There is a little used work-around, and we employ it at our peril.
 *
 */

int
TCP_connect(int s, const struct sockaddr_storage *name, socklen_t namelen, int msec)
{
	int i, k;
	socklen_t l;
	struct pollfd fds[1];

	assert(s >= 0);

	/* Set the socket non-blocking */
	if (msec > 0)
		(void)TCP_nonblocking(s);

	/* Attempt the connect */
	i = connect(s, (const void *)name, namelen);
	if (i == 0 || errno != EINPROGRESS)
		return (i);

	assert(msec > 0);
	/* Exercise our patience, polling for write */
	fds[0].fd = s;
	fds[0].events = POLLWRNORM;
	fds[0].revents = 0;
	i = poll(fds, 1, msec);

	if (i == 0) {
		/* Timeout, close and give up */
		errno = ETIMEDOUT;
		return (-1);
	}

	/* Find out if we got a connection */
	l = sizeof k;
	AZ(getsockopt(s, SOL_SOCKET, SO_ERROR, &k, &l));

	/* An error means no connection established */
	errno = k;
	if (k)
		return (-1);

	(void)TCP_blocking(s);
	return (0);
}

/*--------------------------------------------------------------------
 * When closing a TCP connection, a couple of errno's are legit, we
 * can't be held responsible for the other end wanting to talk to us.
 */

void
TCP_close(int *s)
{
	int i;

	i = close(*s);

	assert (TCP_Check(i));
	*s = -1;
}

void
TCP_set_read_timeout(int s, double seconds)
{
	struct timeval timeout;
	timeout.tv_sec = (int)floor(seconds);
	timeout.tv_usec = (int)(1e6 * (seconds - timeout.tv_sec));
#ifdef SO_RCVTIMEO_WORKS
	AZ(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout));
#else
	(void)s;
#endif
}

/*--------------------------------------------------------------------
 * Set or reset SO_LINGER flag
 */

int
TCP_linger(int sock, int linger)
{
	struct linger lin;
	int i;

	memset(&lin, 0, sizeof lin);
	lin.l_onoff = linger;
	i = setsockopt(sock, SOL_SOCKET, SO_LINGER, &lin, sizeof lin);
	TCP_Assert(i);
	return (i);
}
