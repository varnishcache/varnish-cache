/*-
 * Copyright 2018 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Geoffrey Simmons <geoffrey.simmons@uplex.de>
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

#include <sys/socket.h>
#include <sys/un.h>

#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <stdio.h>

#include "vdef.h"
#include "vas.h"
#include "vus.h"
#include "vtcp.h"

static int
sun_init(struct sockaddr_un *uds, const char *path, const char **err)
{
	AN(uds);
	AN(path);
	assert(VUS_is(path));

	if (err)
		*err = NULL;

	if (strlen(path) + 1 > sizeof(uds->sun_path)) {
		errno = ENAMETOOLONG;
		if (err)
			*err = "Path too long for a Unix domain socket";
		return (-1);
	}
	if (! strcmp(path, "@")) {
		errno = EINVAL;
		if (err)
			*err = "The empty abstract socket name is not"
			    " supported";
		return (-1);
	}
	memset(uds->sun_path, 0, sizeof(uds->sun_path));
	if (*path == '@')
		bprintf(uds->sun_path, "%c%s", 0, path + 1);
	else
		bprintf(uds->sun_path, "%s", path);
	uds->sun_family = PF_UNIX;
	return (0);
}

int
VUS_resolver(const char *path, vus_resolved_f *func, void *priv,
	     const char **err)
{
	struct sockaddr_un uds;
	int ret;

	AN(err);

	ret = sun_init(&uds, path, err);
	if (ret)
		return (ret);

	assert(uds.sun_path[1] != '\0');

	if (func != NULL)
		ret = func(priv, &uds);
	return (ret);
}

int
VUS_bind(const struct sockaddr_un *uds, const char **errp)
{
	int sd, e;
	socklen_t sl;

	sl = VUS_socklen(uds);

	if (errp != NULL)
		*errp = NULL;

	sd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (sd < 0) {
		if (errp != NULL)
			*errp = "socket(2)";
		return (-1);
	}

	if (unlink(uds->sun_path) != 0 && errno != ENOENT) {
		if (errp != NULL)
			*errp = "unlink(2)";
		e = errno;
		closefd(&sd);
		errno = e;
		return (-1);
	}

	if (bind(sd, (const void*)uds, sl) != 0) {
		if (errp != NULL)
			*errp = "bind(2)";
		e = errno;
		closefd(&sd);
		errno = e;
		return (-1);
	}
	return (sd);
}

int
VUS_connect(const char *path, int msec)
{
	int s, i;
	struct pollfd fds[1];
	struct sockaddr_un uds;
	socklen_t sl;

	if (path == NULL)
		return (-1);
	i = sun_init(&uds, path, NULL);
	if (i)
		return (i);

	assert(uds.sun_path[1] != '\0');

	sl = VUS_socklen(&uds);

	AN(sl);

	s = socket(PF_UNIX, SOCK_STREAM, 0);
	if (s < 0)
		return (s);

	/* Set the socket non-blocking */
	if (msec != 0)
		VTCP_nonblocking(s);

	i = connect(s, (const void*)&uds, sl);
	if (i == 0 && msec > 0)
		VTCP_blocking(s);
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

socklen_t
VUS_socklen(const struct sockaddr_un *uds)
{
	socklen_t sl;
	const char *p;
	if (*uds->sun_path)
		sl = sizeof(*uds);
	else {
		p = strchr(uds->sun_path + 1, '\0');
		assert(p != NULL);
		sl = p - (const char*)uds;
	}
	assert(sl <= sizeof(*uds));
	return sl;
}
