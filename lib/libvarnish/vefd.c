/*-
 * Copyright (c) 2025 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "config.h"

#if HAVE_EVENTFD
#  include <sys/eventfd.h>
#else
#  include <fcntl.h>
#endif

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include <vdef.h>
#include <vefd.h>
#include <vas.h>
#include <miniobj.h>

#if HAVE_EVENTFD
int
VEFD_Open(struct vefd *vefd)
{

	CHECK_OBJ_NOTNULL(vefd, VEFD_MAGIC);
	assert(vefd->poll_fd == -1);
	assert(vefd->priv_fd == -1);

	vefd->poll_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	return (vefd->poll_fd);
}

int
VEFD_Signal(struct vefd *vefd)
{
	int64_t buf = 1;

	CHECK_OBJ_NOTNULL(vefd, VEFD_MAGIC);
	assert(vefd->poll_fd >= 0);
	assert(vefd->priv_fd == -1);
	assert(write(vefd->poll_fd, &buf, sizeof buf) == sizeof buf);
	return (0);
}

int
VEFD_Clear(struct vefd *vefd)
{
	int64_t buf;

	CHECK_OBJ_NOTNULL(vefd, VEFD_MAGIC);
	assert(vefd->poll_fd >= 0);
	assert(vefd->priv_fd == -1);
	assert(read(vefd->poll_fd, &buf, sizeof buf) == sizeof buf);
	return (0);
}

int
VEFD_Close(struct vefd *vefd)
{

	CHECK_OBJ_NOTNULL(vefd, VEFD_MAGIC);
	assert(vefd->poll_fd >= 0);
	assert(vefd->priv_fd == -1);
	closefd(&vefd->poll_fd);
	return (0);
}
#else /* !HAVE_EVENTFD */
int
VEFD_Open(struct vefd *vefd)
{
	int fd[2];

	CHECK_OBJ_NOTNULL(vefd, VEFD_MAGIC);
	assert(vefd->poll_fd == -1);
	assert(vefd->priv_fd == -1);

	if (pipe(fd) < 0)
		return (-1);

	AZ(fcntl(fd[0], F_SETFL, O_CLOEXEC|O_NONBLOCK));
	AZ(fcntl(fd[1], F_SETFL, O_CLOEXEC|O_NONBLOCK));
	vefd->poll_fd = fd[0];
	vefd->priv_fd = fd[1];
	return (0);
}

int
VEFD_Signal(struct vefd *vefd)
{
	ssize_t r;

	CHECK_OBJ_NOTNULL(vefd, VEFD_MAGIC);
	assert(vefd->poll_fd >= 0);
	assert(vefd->priv_fd >= 0);
	assert(vefd->poll_fd != vefd->priv_fd);
	r = write(vefd->priv_fd, "", 1);
	if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		return (-1);
	return (0);
}

int
VEFD_Clear(struct vefd *vefd)
{
	char buf[64];
	ssize_t r;

	CHECK_OBJ_NOTNULL(vefd, VEFD_MAGIC);
	assert(vefd->poll_fd >= 0);
	assert(vefd->priv_fd >= 0);
	assert(vefd->poll_fd != vefd->priv_fd);
	do {
		r = read(vefd->poll_fd, buf, sizeof buf);
	} while (r > 0);
	if (errno != EAGAIN && errno != EWOULDBLOCK)
		return (-1);
	return (0);
}

int
VEFD_Close(struct vefd *vefd)
{

	CHECK_OBJ_NOTNULL(vefd, VEFD_MAGIC);
	assert(vefd->poll_fd >= 0);
	assert(vefd->priv_fd >= 0);
	assert(vefd->poll_fd != vefd->priv_fd);
	closefd(&vefd->poll_fd);
	closefd(&vefd->priv_fd);
	return (0);
}
#endif /* HAVE_EVENTFD */
