/*-
 * Copyright (c) 2018 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Geoffrey Simmons <geoffrey.simmons@uplex.de>
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
#include <errno.h>

#include "vdef.h"
#include "vas.h"
#include "vus.h"

int
VUS_resolver(const char *path, vus_resolved_f *func, void *priv,
	     const char **err)
{
	struct sockaddr_un uds;
	int ret = 0;

	AN(path);
	assert(*path == '/');

	AN(err);
	*err = NULL;
	if (strlen(path) + 1 > sizeof(uds.sun_path)) {
		*err = "Path too long for a Unix domain socket";
		return(-1);
	}
	strcpy(uds.sun_path, path);
	uds.sun_family = PF_UNIX;
	if (func != NULL)
		ret = func(priv, &uds);
	return(ret);
}

int
VUS_bind(const struct sockaddr_un *uds, const char **errp)
{
	int sd, e;
	socklen_t sl = sizeof(*uds);

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

	if (bind(sd, (const struct sockaddr *)uds, sl) != 0) {
		if (errp != NULL)
			*errp = "bind(2)";
		e = errno;
		closefd(&sd);
		errno = e;
		return (-1);
	}
	return (sd);
}
