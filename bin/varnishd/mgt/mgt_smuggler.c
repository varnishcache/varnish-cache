/*-
 * Copyright (c) 2023 Varnish Software AS
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"

static int smug_fd = -1;

int
mgt_SMUG_Init(void)
{
	int sock[2];

	assert(smug_fd < 0);
	AZ(socketpair(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, PF_UNIX, sock));
	smug_fd = sock[0];
	return (sock[1]);
}

void
mgt_SMUG_Fini(void)
{

	closefd(&smug_fd);
}

static uint64_t
nonce_recv(void)
{
	uint64_t r;
	ssize_t l;

	do {
		l = recv(smug_fd, &r, sizeof r, MSG_WAITALL);
	} while (l < 0 && errno == EINTR);
	if (l == 0) {
		return (0);
	}
	assert(l == sizeof r);
	return (r);
}

uint64_t
mgt_smuggle(int fd)
{
	struct iovec iov;
	struct msghdr msg;
	struct cmsghdr *cmsg;
	union {
		char cmsg_buf[CMSG_SPACE(sizeof fd)];
		struct cmsghdr align;
	} u;
	uint64_t nonce;
	ssize_t l;

	assert(fd >= 0);

	if (smug_fd < 0)
		return (0);

	nonce = 0;
	iov.iov_base = &nonce;
	iov.iov_len = sizeof nonce;

	memset(&msg, 0, sizeof msg);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = u.cmsg_buf;
	msg.msg_controllen = sizeof u.cmsg_buf;

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof fd);
	memcpy(CMSG_DATA(cmsg), &fd, sizeof fd);

	do {
		l = sendmsg(smug_fd, &msg, MSG_NOSIGNAL);
	} while (l < 0 && errno == EINTR);
	if (l < 0) {
		/* Presumably the child has died. The socket was not
		 * dup'ed into the child. */
		assert(errno == EPIPE);
		return (0);
	}

	nonce = nonce_recv();
	return (nonce);
}

int
mgt_SMUG_Cancel(uint64_t nonce)
{
	ssize_t l;
	uint64_t r;

	AN(nonce);

	if (smug_fd < 0) {
		/* Child not running, so it can't can't be holding the
		 * socket. Report success */
		return (0);
	}

	do {
		l = send(smug_fd, &nonce, sizeof nonce, MSG_NOSIGNAL);
	} while (l < 0 && errno == EINTR);
	if (l < 0) {
		/* Presumably the child has died. The remote socket will
		 * be closed in the process. Report success. */
		assert(errno == EPIPE);
		return (0);
	}
	assert(l == sizeof nonce);

	r = nonce_recv();
	if (r == 0)
		return (0); /* Child already gone.. Report success. */
	if (r != nonce)
		return (-1); /* Socket already consumed. Report failure. */

	/* Socket was successfully removed from the incoming socket list
	 * and closed in the child process */
	return (0);
}
