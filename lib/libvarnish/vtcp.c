/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_FILIO_H
#  include <sys/filio.h>
#endif

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <errno.h>
#include <math.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "vdef.h"
#include "vas.h"
#include "vsa.h"
#include "vss.h"
#include "vtcp.h"

/*--------------------------------------------------------------------*/
static void
vtcp_sa_to_ascii(const void *sa, socklen_t l, char *abuf, unsigned alen,
    char *pbuf, unsigned plen)
{
	int i;

	assert(abuf == NULL || alen > 0);
	assert(pbuf == NULL || plen > 0);
	i = getnameinfo(sa, l, abuf, alen, pbuf, plen,
	   NI_NUMERICHOST | NI_NUMERICSERV);
	if (i) {
		/*
		 * XXX this printf is shitty, but we may not have space
		 * for the gai_strerror in the bufffer :-(
		 */
		printf("getnameinfo = %d %s\n", i, gai_strerror(i));
		if (abuf != NULL)
			(void)snprintf(abuf, alen, "Conversion");
		if (pbuf != NULL)
			(void)snprintf(pbuf, plen, "Failed");
		return;
	}
	/* XXX dirty hack for v4-to-v6 mapped addresses */
	if (abuf != NULL && strncmp(abuf, "::ffff:", 7) == 0) {
		for (i = 0; abuf[i + 7]; ++i)
			abuf[i] = abuf[i + 7];
		abuf[i] = '\0';
	}
}

/*--------------------------------------------------------------------*/

void
VTCP_name(const struct suckaddr *addr, char *abuf, unsigned alen,
    char *pbuf, unsigned plen)
{
	const struct sockaddr *sa;
	socklen_t sl;

	sa = VSA_Get_Sockaddr(addr, &sl);
	vtcp_sa_to_ascii(sa, sl, abuf, alen, pbuf, plen);
}

/*--------------------------------------------------------------------*/

struct suckaddr *
VTCP_my_suckaddr(int sock)
{
	struct sockaddr_storage addr_s;
	socklen_t l;

	l = sizeof addr_s;
	AZ(getsockname(sock, (void *)&addr_s, &l));
	return (VSA_Malloc(&addr_s, l));
}

/*--------------------------------------------------------------------*/

void
VTCP_myname(int sock, char *abuf, unsigned alen, char *pbuf, unsigned plen)
{
	struct sockaddr_storage addr_s;
	socklen_t l;

	l = sizeof addr_s;
	AZ(getsockname(sock, (void *)&addr_s, &l));
	vtcp_sa_to_ascii(&addr_s, l, abuf, alen, pbuf, plen);
}

/*--------------------------------------------------------------------*/

void
VTCP_hisname(int sock, char *abuf, unsigned alen, char *pbuf, unsigned plen)
{
	struct sockaddr_storage addr_s;
	socklen_t l;

	l = sizeof addr_s;
	if (!getpeername(sock, (void*)&addr_s, &l))
		vtcp_sa_to_ascii(&addr_s, l, abuf, alen, pbuf, plen);
	else {
		(void)snprintf(abuf, alen, "<none>");
		(void)snprintf(pbuf, plen, "<none>");
	}
}

/*--------------------------------------------------------------------*/

#ifdef HAVE_ACCEPT_FILTERS

int
VTCP_filter_http(int sock)
{
	int retval;
	struct accept_filter_arg afa;

	memset(&afa, 0, sizeof(afa));
	strcpy(afa.af_name, "httpready");
	retval = setsockopt(sock, SOL_SOCKET, SO_ACCEPTFILTER,
	    &afa, sizeof afa );
	return (retval);
}

#elif defined(__linux)

int
VTCP_filter_http(int sock)
{
	int retval;
	int defer = 1;

	retval = setsockopt(sock, SOL_TCP,TCP_DEFER_ACCEPT,
	    &defer, sizeof defer);
	return (retval);
}

#else

int
VTCP_filter_http(int sock)
{
	errno = EOPNOTSUPP;
	(void)sock;
	return (-1);
}

#endif

/*--------------------------------------------------------------------*/

#ifdef HAVE_TCP_FASTOPEN

int
VTCP_fastopen(int sock, int depth)
{
	return (setsockopt(sock, SOL_TCP, TCP_FASTOPEN,
	    &depth, sizeof depth));
}

#else

int
VTCP_fastopen(int sock, int depth)
{
	errno = EOPNOTSUPP;
	(void)sock;
	(void)depth;
	return (-1);
}

#endif

/*--------------------------------------------------------------------
 * Functions for controlling NONBLOCK mode.
 *
 * We use FIONBIO because it is cheaper than fcntl(2), which requires
 * us to do two syscalls, one to get and one to set, the latter of
 * which mucks about a bit before it ends up calling ioctl(FIONBIO),
 * at least on FreeBSD.
 */

int
VTCP_blocking(int sock)
{
	int i, j;

	i = 0;
	j = ioctl(sock, FIONBIO, &i);
	VTCP_Assert(j);
	return (j);
}

int
VTCP_nonblocking(int sock)
{
	int i, j;

	i = 1;
	j = ioctl(sock, FIONBIO, &i);
	VTCP_Assert(j);
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
VTCP_connected(int s)
{
	int k;
	socklen_t l;

	/* Find out if we got a connection */
	l = sizeof k;
	AZ(getsockopt(s, SOL_SOCKET, SO_ERROR, &k, &l));

	/* An error means no connection established */
	errno = k;
	if (k) {
		closefd(&s);
		return (-1);
	}

	(void)VTCP_blocking(s);
	return (s);
}

int
VTCP_connect(const struct suckaddr *name, int msec)
{
	int s, i;
	struct pollfd fds[1];
	const struct sockaddr *sa;
	socklen_t sl;
	int val;

	if (name == NULL)
		return (-1);
	/* Attempt the connect */
	AN(VSA_Sane(name));
	sa = VSA_Get_Sockaddr(name, &sl);
	AN(sa);
	AN(sl);

	s = socket(sa->sa_family, SOCK_STREAM, 0);
	if (s < 0)
		return (s);

	/* Set the socket non-blocking */
	if (msec != 0)
		(void)VTCP_nonblocking(s);

	val = 1;
	AZ(setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &val, sizeof val));

	i = connect(s, sa, sl);
	if (i == 0)
		return (s);
	if (errno != EINPROGRESS) {
		closefd(&s);
		return (-1);
	}

	if (msec < 0) {
		/*
		 * Caller is responsible for waiting and
		 * calling VTCP_connected
		 */
		return (s);
	}

	assert(msec > 0);
	/* Exercise our patience, polling for write */
	fds[0].fd = s;
	fds[0].events = POLLWRNORM;
	fds[0].revents = 0;
	i = poll(fds, 1, msec);

	if (i == 0) {
		/* Timeout, close and give up */
		closefd(&s);
		errno = ETIMEDOUT;
		return (-1);
	}

	return (VTCP_connected(s));
}

/*--------------------------------------------------------------------
 * When closing a TCP connection, a couple of errno's are legit, we
 * can't be held responsible for the other end wanting to talk to us.
 */

void
VTCP_close(int *s)
{
	int i;

	i = close(*s);

	assert(VTCP_Check(i));
	*s = -1;
}

void
VTCP_set_read_timeout(int s, double seconds)
{
#ifdef SO_RCVTIMEO_WORKS
	struct timeval timeout;
	timeout.tv_sec = (int)floor(seconds);
	timeout.tv_usec = (int)(1e6 * (seconds - timeout.tv_sec));
	/*
	 * Solaris bug (present at least in snv_151 and older): If this fails
	 * with EINVAL, the socket is half-closed (SS_CANTSENDMORE) and the
	 * timeout does not get set. Needs to be fixed in Solaris, there is
	 * nothing we can do about this.
	 */
	VTCP_Assert(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
	    &timeout, sizeof timeout));
#else
	(void)s;
	(void)seconds;
#endif
}

/*--------------------------------------------------------------------
 */

static int v_matchproto_(vss_resolved_f)
vtcp_open_callback(void *priv, const struct suckaddr *sa)
{
	double *p = priv;

	return (VTCP_connect(sa, (int)floor(*p * 1e3)));
}

int
VTCP_open(const char *addr, const char *def_port, double timeout,
    const char **errp)
{
	int error;
	const char *err;

	if (errp != NULL)
		*errp = NULL;
	assert(timeout >= 0);
	error = VSS_resolver(addr, def_port, vtcp_open_callback,
	    &timeout, &err);
	if (err != NULL) {
		if (errp != NULL)
			*errp = err;
		return (-1);
	}
	return (error);
}

/*--------------------------------------------------------------------
 * Given a struct suckaddr, open a socket of the appropriate type, and bind
 * it to the requested address.
 *
 * If the address is an IPv6 address, the IPV6_V6ONLY option is set to
 * avoid conflicts between INADDR_ANY and IN6ADDR_ANY.
 */

int
VTCP_bind(const struct suckaddr *sa, const char **errp)
{
	int sd, val, e;
	socklen_t sl;
	const struct sockaddr *so;
	int proto;

	if (errp != NULL)
		*errp = NULL;

	proto = VSA_Get_Proto(sa);
	sd = socket(proto, SOCK_STREAM, 0);
	if (sd < 0) {
		if (errp != NULL)
			*errp = "socket(2)";
		return (-1);
	}
	val = 1;
	if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) != 0) {
		if (errp != NULL)
			*errp = "setsockopt(SO_REUSEADDR, 1)";
		e = errno;
		closefd(&sd);
		errno = e;
		return (-1);
	}
#ifdef IPV6_V6ONLY
	/* forcibly use separate sockets for IPv4 and IPv6 */
	val = 1;
	if (proto == AF_INET6 &&
	    setsockopt(sd, IPPROTO_IPV6, IPV6_V6ONLY, &val, sizeof val) != 0) {
		if (errp != NULL)
			*errp = "setsockopt(IPV6_V6ONLY, 1)";
		e = errno;
		closefd(&sd);
		errno = e;
		return (-1);
	}
#endif
	so = VSA_Get_Sockaddr(sa, &sl);
	if (bind(sd, so, sl) != 0) {
		if (errp != NULL)
			*errp = "bind(2)";
		e = errno;
		closefd(&sd);
		errno = e;
		return (-1);
	}
	return (sd);
}

/*--------------------------------------------------------------------
 * Given a struct suckaddr, open a socket of the appropriate type, bind it
 * to the requested address, and start listening.
 */

int
VTCP_listen(const struct suckaddr *sa, int depth, const char **errp)
{
	int sd;
	int e;

	if (errp != NULL)
		*errp = NULL;
	sd = VTCP_bind(sa, errp);
	if (sd >= 0)  {
		if (listen(sd, depth) != 0) {
			e = errno;
			closefd(&sd);
			errno = e;
			if (errp != NULL)
				*errp = "listen(2)";
			return (-1);
		}
	}
	return (sd);
}

/*--------------------------------------------------------------------*/

struct helper {
	int		depth;
	const char	**errp;
};

static int v_matchproto_(vss_resolved_f)
vtcp_lo_cb(void *priv, const struct suckaddr *sa)
{
	int sock;
	struct helper *hp = priv;

	sock = VTCP_listen(sa, hp->depth, hp->errp);
	if (sock > 0) {
		*hp->errp = NULL;
		return (sock);
	}
	AN(*hp->errp);
	return (0);
}

int
VTCP_listen_on(const char *addr, const char *def_port, int depth,
    const char **errp)
{
	struct helper h;
	int sock;

	h.depth = depth;
	h.errp = errp;

	sock = VSS_resolver(addr, def_port, vtcp_lo_cb, &h, errp);
	if (*errp != NULL)
		return (-1);
	return(sock);
}

/*--------------------------------------------------------------------
 * Set or reset SO_LINGER flag
 */

int
VTCP_linger(int sock, int linger)
{
	struct linger lin;
	int i;

	memset(&lin, 0, sizeof lin);
	lin.l_onoff = linger;
	i = setsockopt(sock, SOL_SOCKET, SO_LINGER, &lin, sizeof lin);
	VTCP_Assert(i);
	return (i);
}

/*--------------------------------------------------------------------
 * Do a poll to check for remote HUP
 */

int
VTCP_check_hup(int sock)
{
	struct pollfd pfd;

	assert(sock > 0);
	pfd.fd = sock;
	pfd.events = POLLOUT;
	pfd.revents = 0;

	if (poll(&pfd, 1, 0) == 1 && pfd.revents & POLLHUP)
		return (1);
	return (0);
}

/*--------------------------------------------------------------------
 * Check if a TCP syscall return value is fatal
 */

int
VTCP_Check(int a)
{
	if (a == 0)
		return (1);
	if (errno == ECONNRESET || errno == ENOTCONN || errno == EPIPE)
		return (1);
#if (defined (__SVR4) && defined (__sun)) || defined (__NetBSD__)
	/*
	 * Solaris returns EINVAL if the other end unexpectedly reset the
	 * connection.
	 * This is a bug in Solaris and documented behaviour on NetBSD.
	 */
	if (errno == EINVAL || errno == ETIMEDOUT)
		return (1);
#elif defined (__APPLE__)
	/*
	 * MacOS returns EINVAL if the other end unexpectedly reset
	 * the connection.
	 */
	if (errno == EINVAL)
		return (1);
#endif
	return (0);
}

/*--------------------------------------------------------------------
 *
 */

int
VTCP_read(int fd, void *ptr, size_t len, double tmo)
{
	struct pollfd pfd[1];
	int i, j;

	if (tmo > 0.0) {
		pfd[0].fd = fd;
		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		j = (int)floor(tmo * 1e3);
		if (j == 0)
			j++;
		j = poll(pfd, 1, j);
		if (j == 0)
			return (-2);
	}
	i = read(fd, ptr, len);
	return (i < 0 ? -1 : i);
}
