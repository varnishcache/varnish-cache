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
#include <stdlib.h>
#include <string.h>

#include "cache/cache_varnishd.h"
#include "common/heritage.h"

#include "miniobj.h"
#include "vrnd.h"

struct loot {
	unsigned		magic;
#define LOOT_MAGIC		0xb91eb902
	int			fd;
	VTAILQ_ENTRY(loot)	list;
	uint64_t		nonce;
};

static struct lock		smug_mtx;
static VTAILQ_HEAD(, loot)	contraband;

static struct loot *
loot_snatch(uint64_t nonce)
{
	struct loot *loot;

	Lck_AssertHeld(&smug_mtx);
	VTAILQ_FOREACH(loot, &contraband, list) {
		CHECK_OBJ(loot, LOOT_MAGIC);
		if (loot->nonce == nonce) {
			VTAILQ_REMOVE(&contraband, loot, list);
			break;
		}
	}

	return (loot);
}

static uint64_t
nonce_gen(void)
{
	struct loot *loot;
	uint64_t nonce;

	Lck_AssertHeld(&smug_mtx);
	do {
		do {
			AZ(VRND_RandomCrypto(&nonce, sizeof nonce));
		} while (nonce == 0);

		VTAILQ_FOREACH(loot, &contraband, list) {
			CHECK_OBJ(loot, LOOT_MAGIC);
			if (nonce == loot->nonce)
				break;
		}
	} while (loot != NULL);

	return (nonce);
}

static void * v_matchproto_(bgthread_t)
smug_thread(struct worker *wrk, void *priv)
{
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec iov;
	union {
		char cmsg_buf[CMSG_SPACE(sizeof (int))];
		struct cmsghdr align;
	} u;
	struct loot *loot;
	uint64_t nonce;
	ssize_t l;
	int fd;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AZ(priv);

	while (1) {
		iov.iov_base = &nonce;
		iov.iov_len = sizeof nonce;

		memset(&msg, 0, sizeof msg);
		memset(&u, 0, sizeof u);
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = u.cmsg_buf;
		msg.msg_controllen = sizeof u.cmsg_buf;

		do {
			l = recvmsg(heritage.fence, &msg, MSG_CMSG_CLOEXEC);
		} while (l < 0 && errno == EINTR);
		if (l == 0) {
			/* MGT has closed the connection. */
			break;
		}
		assert(l == sizeof nonce);

		if (nonce > 0) {
			/* Close stored fd on that nonce */
			l = 0;
			Lck_Lock(&smug_mtx);
			loot = loot_snatch(nonce);
			Lck_Unlock(&smug_mtx);
			if (loot == NULL)
				nonce = UINT64_MAX;
			else {
				closefd(&loot->fd);
				FREE_OBJ(loot);
			}
		} else {
			AZ(msg.msg_flags & MSG_CTRUNC);
			cmsg = CMSG_FIRSTHDR(&msg);
			AN(cmsg);
			assert(cmsg->cmsg_level == SOL_SOCKET);
			assert(cmsg->cmsg_type == SCM_RIGHTS);
			assert(cmsg->cmsg_len == CMSG_LEN(sizeof fd));

			ALLOC_OBJ(loot, LOOT_MAGIC);
			AN(loot);
			memcpy(&loot->fd, CMSG_DATA(cmsg), sizeof fd);

			Lck_Lock(&smug_mtx);
			loot->nonce = nonce_gen();
			VTAILQ_INSERT_TAIL(&contraband, loot, list);
			Lck_Unlock(&smug_mtx);

			nonce = loot->nonce;
		}

		do {
			l = send(heritage.fence, &nonce, sizeof nonce,
			    MSG_NOSIGNAL);
		} while (l < 0 && errno == EINTR);
		if (l < 0) {
			/* MGT has closed the connection */
			break;
		}
	}

	return (NULL);
}

int
SMUG_Fence(uint64_t nonce)
{
	struct loot *loot;
	int fd;

	Lck_Lock(&smug_mtx);
	loot = loot_snatch(nonce);
	Lck_Unlock(&smug_mtx);

	if (loot == NULL)
		return (-1);

	fd = loot->fd;
	assert(fd > 0);
	FREE_OBJ(loot);
	return (fd);
}

void
SMUG_Init(void)
{
	pthread_t thread;

	assert(heritage.fence >= 0);
	Lck_New(&smug_mtx, lck_cli);
	VTAILQ_INIT(&contraband);

	WRK_BgThread(&thread, "fd-smuggler", smug_thread, NULL);
}
