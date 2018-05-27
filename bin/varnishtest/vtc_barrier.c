/*-
 * Copyright (c) 2005 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dridi Boukelmoune <dridi@varnish-software.com>
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

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/time.h> /* for MUSL */

#include "vtc.h"
#include "vtcp.h"

enum barrier_e {
	BARRIER_NONE = 0,
	BARRIER_COND,
	BARRIER_SOCK,
};

struct barrier {
	unsigned		magic;
#define BARRIER_MAGIC		0x7b54c275
	char			*name;
	VTAILQ_ENTRY(barrier)	list;
	pthread_mutex_t		mtx;
	pthread_cond_t		cond;

	unsigned		waiters;
	unsigned		expected;
	unsigned		cyclic;

	enum barrier_e		type;
	/* fields below are only for BARRIER_SOCK */
	pthread_t		thread;
	volatile unsigned	active;
	volatile unsigned	need_join;
};

static VTAILQ_HEAD(, barrier)	barriers = VTAILQ_HEAD_INITIALIZER(barriers);

static struct barrier *
barrier_new(const char *name, struct vtclog *vl)
{
	struct barrier *b;

	ALLOC_OBJ(b, BARRIER_MAGIC);
	AN(b);
	if (pthread_self() != vtc_thread)
		vtc_fatal(vl,
		    "Barrier %s can only be created on the top thread", name);
	REPLACE(b->name, name);

	AZ(pthread_mutex_init(&b->mtx, NULL));
	AZ(pthread_cond_init(&b->cond, NULL));
	b->waiters = 0;
	b->expected = 0;
	VTAILQ_INSERT_TAIL(&barriers, b, list);
	return (b);
}

/**********************************************************************
 * Init a barrier
 */

static void
barrier_expect(struct barrier *b, const char *av, struct vtclog *vl)
{
	unsigned expected;

	if (b->type != BARRIER_NONE)
		vtc_fatal(vl,
		    "Barrier(%s) use error: already initialized", b->name);

	AZ(b->expected);
	AZ(b->waiters);
	expected = strtoul(av, NULL, 0);
	if (expected < 2)
		vtc_fatal(vl,
		    "Barrier(%s) use error: wrong expectation (%u)",
		    b->name, expected);

	b->expected = expected;
}

static void
barrier_cond(struct barrier *b, const char *av, struct vtclog *vl)
{

	CHECK_OBJ_NOTNULL(b, BARRIER_MAGIC);
	barrier_expect(b, av, vl);
	b->type = BARRIER_COND;
}

static void *
barrier_sock_thread(void *priv)
{
	struct barrier *b;
	struct vtclog *vl;
	const char *err;
	char abuf[16], pbuf[6];
	int i, sock, *conns;
	struct pollfd pfd[1];

	CAST_OBJ_NOTNULL(b, priv, BARRIER_MAGIC);
	assert(b->type == BARRIER_SOCK);

	AZ(pthread_mutex_lock(&b->mtx));

	vl = vtc_logopen(b->name);
	pthread_cleanup_push(vtc_logclose, vl);

	sock = VTCP_listen_on("127.0.0.1:0", NULL, b->expected, &err);
	if (sock < 0) {
		AZ(pthread_cond_signal(&b->cond));
		AZ(pthread_mutex_unlock(&b->mtx));
		vtc_fatal(vl, "Barrier(%s) %s fails: %s (errno=%d)",
		    b->name, err, strerror(errno), errno);
	}
	assert(sock > 0);
	(void)VTCP_nonblocking(sock);
	VTCP_myname(sock, abuf, sizeof abuf, pbuf, sizeof pbuf);

	macro_def(vl, b->name, "addr", "%s", abuf);
	macro_def(vl, b->name, "port", "%s", pbuf);
	macro_def(vl, b->name, "sock", "%s:%s", abuf, pbuf);

	AZ(pthread_cond_signal(&b->cond));
	AZ(pthread_mutex_unlock(&b->mtx));

	conns = calloc(b->expected, sizeof *conns);
	AN(conns);

	while (b->active) {
		pfd[0].fd = sock;
		pfd[0].events = POLLIN;

		i = poll(pfd, 1, 1000);
		if (i == 0)
			continue;
		if (i < 0) {
			if (errno == EINTR)
				continue;
			closefd(&sock);
			vtc_fatal(vl,
			    "Barrier(%s) select fails: %s (errno=%d)",
			    b->name, strerror(errno), errno);
		}
		assert(i == 1);
		assert(b->waiters <= b->expected);
		if (b->waiters == b->expected)
			vtc_fatal(vl,
			    "Barrier(%s) use error: "
			    "more waiters than the %u expected",
			    b->name, b->expected);

		i = accept(sock, NULL, NULL);
		if (i < 0) {
			closefd(&sock);
			vtc_fatal(vl,
			    "Barrier(%s) accept fails: %s (errno=%d)",
			    b->name, strerror(errno), errno);
		}

		/* NB. We don't keep track of the established connections, only
		 *     that connections were made to the barrier's socket.
		 */
		conns[b->waiters] = i;

		if (++b->waiters < b->expected) {
			vtc_log(vl, 4, "Barrier(%s) wait %u of %u",
			    b->name, b->waiters, b->expected);
			continue;
		}

		vtc_log(vl, 4, "Barrier(%s) wake %u", b->name, b->expected);
		for (i = 0; i < b->expected; i++)
			closefd(&conns[i]);

		if (b->cyclic)
			b->waiters = 0;
		else
			b->active = 0;
	}

	macro_undef(vl, b->name, "addr");
	macro_undef(vl, b->name, "port");
	macro_undef(vl, b->name, "sock");
	closefd(&sock);
	free(conns);
	pthread_cleanup_pop(0);
	vtc_logclose(vl);
	return (NULL);
}

static void
barrier_sock(struct barrier *b, const char *av, struct vtclog *vl)
{

	CHECK_OBJ_NOTNULL(b, BARRIER_MAGIC);
	barrier_expect(b, av, vl);
	b->type = BARRIER_SOCK;
	b->active = 1;
	b->need_join = 1;

	/* NB. We can use the BARRIER_COND's pthread_cond_t to wait until the
	 *     socket is ready for convenience.
	 */
	AZ(pthread_create(&b->thread, NULL, barrier_sock_thread, b));
	AZ(pthread_cond_wait(&b->cond, &b->mtx));
}

static void
barrier_cyclic(struct barrier *b, struct vtclog *vl)
{

	CHECK_OBJ_NOTNULL(b, BARRIER_MAGIC);

	if (b->type == BARRIER_NONE)
		vtc_fatal(vl,
		    "Barrier(%s) use error: not initialized", b->name);

	if (b->waiters != 0)
		vtc_fatal(vl,
		    "Barrier(%s) use error: already in use", b->name);

	b->cyclic = 1;
}

/**********************************************************************
 * Sync a barrier
 */

static void
barrier_cond_sync(struct barrier *b, struct vtclog *vl)
{

	CHECK_OBJ_NOTNULL(b, BARRIER_MAGIC);
	assert(b->type == BARRIER_COND);

	assert(b->waiters <= b->expected);
	if (b->waiters == b->expected)
		vtc_fatal(vl,
		    "Barrier(%s) use error: more waiters than the %u expected",
		    b->name, b->expected);

	if (++b->waiters == b->expected) {
		vtc_log(vl, 4, "Barrier(%s) wake %u", b->name, b->expected);
		AZ(pthread_cond_broadcast(&b->cond));
	} else {
		vtc_log(vl, 4, "Barrier(%s) wait %u of %u",
		    b->name, b->waiters, b->expected);
		AZ(pthread_cond_wait(&b->cond, &b->mtx));
	}

	if (b->cyclic)
		b->waiters = 0;
}

static void
barrier_sock_sync(struct barrier *b, struct vtclog *vl)
{
	struct vsb *vsb;
	const char *err;
	char buf[32];
	int i, sock;
	ssize_t sz;

	CHECK_OBJ_NOTNULL(b, BARRIER_MAGIC);
	assert(b->type == BARRIER_SOCK);

	vsb = macro_expandf(vl, "${%s_sock}", b->name);
	vtc_log(vl, 4, "Barrier(%s) sync with socket", b->name);

	sock = VTCP_open(VSB_data(vsb), NULL, 0., &err);
	if (sock < 0)
		vtc_fatal(vl, "Barrier(%s) connection failed: %s",
		    b->name, err);

	VSB_delete(vsb);

	/* emulate pthread_cond_wait's behavior */
	AZ(pthread_mutex_unlock(&b->mtx));
	sz = read(sock, buf, sizeof buf); /* XXX loop with timeout? */
	AZ(pthread_mutex_lock(&b->mtx));

	i = errno;
	closefd(&sock);

	if (sz < 0)
		vtc_fatal(vl, "Barrier(%s) read failed: %s (errno=%d)",
		    b->name, strerror(i), i);
	if (sz > 0)
		vtc_fatal(vl, "Barrier(%s) unexpected data (%zdB)",
		    b->name, sz);
}

static void
barrier_sync(struct barrier *b, struct vtclog *vl)
{

	CHECK_OBJ_NOTNULL(b, BARRIER_MAGIC);

	switch (b->type) {
	case BARRIER_NONE:
		vtc_fatal(vl,
		    "Barrier(%s) use error: not initialized", b->name);
		break;
	case BARRIER_COND:
		barrier_cond_sync(b, vl);
		break;
	case BARRIER_SOCK:
		barrier_sock_sync(b, vl);
		break;
	default:
		WRONG("Wrong barrier type");
	}
}

/* SECTION: barrier barrier
 *
 * NOTE: this can be used from the top-level as well as from client and server
 * specifications.
 *
 * Barriers allows you to synchronize different threads to make sure events
 * occur in the right order. It's even possible to use them in VCL.
 *
 * First, it's necessary to declare the barrier::
 *
 *         barrier bNAME TYPE NUMBER [-cyclic]
 *
 * With the arguments being:
 *
 * bNAME
 *         this is the name of the barrier, used to identify it when you'll
 *         create sync points. It must start with 'b'.
 *
 * TYPE
 *         it can be "cond" (mutex) or "sock" (socket) and sets internal
 *         behavior. If you don't need VCL synchronization, use cond.
 *
 * NUMBER
 *         number of sync point needed to go through the barrier.
 *
 * \-cyclic
 *         if present, the barrier will reset itself and be ready for another
 *         round once gotten through.
 *
 * Then, to add a sync point::
 *
 *         barrier bNAME sync
 *
 * This will block the parent thread until the number of sync points for bNAME
 * reaches the NUMBER given in the barrier declaration.
 *
 * If you wish to synchronize the VCL, you need to declare a "sock" barrier.
 * This will emit a macro definition named "bNAME_sock" that you can use in
 * VCL (after importing the debug vmod)::
 *
 *         debug.barrier_sync("${bNAME_sock}");
 *
 * This function returns 0 if everything went well and is the equivalent of
 * ``barrier bNAME sync`` at the VTC top-level.
 *
 *
 */

void
cmd_barrier(CMD_ARGS)
{
	struct barrier *b, *b2;
	int r;

	(void)priv;
	(void)cmd;

	if (av == NULL) {
		/* Reset and free */
		VTAILQ_FOREACH_SAFE(b, &barriers, list, b2) {
			r = pthread_mutex_trylock(&b->mtx);
			assert(r == 0 || r == EBUSY);
			switch (b->type) {
			case BARRIER_COND:
				break;
			case BARRIER_SOCK:
				if (b->need_join) {
					b->active = 0;
					AZ(pthread_join(b->thread, NULL));
					b->need_join = 0;
				}
				break;
			default:
				WRONG("Wrong barrier type");
			}
			if (r == 0)
				AZ(pthread_mutex_unlock(&b->mtx));
		}
		return;
	}

	AZ(strcmp(av[0], "barrier"));
	av++;

	VTC_CHECK_NAME(vl, av[0], "Barrier", 'b');
	VTAILQ_FOREACH(b, &barriers, list)
		if (!strcmp(b->name, av[0]))
			break;
	if (b == NULL)
		b = barrier_new(av[0], vl);
	av++;

	AZ(pthread_mutex_lock(&b->mtx));
	for (; *av != NULL; av++) {
		if (!strcmp(*av, "cond")) {
			av++;
			AN(*av);
			barrier_cond(b, *av, vl);
			continue;
		}
		if (!strcmp(*av, "sock")) {
			av++;
			AN(*av);
			barrier_sock(b, *av, vl);
			continue;
		}
		if (!strcmp(*av, "sync")) {
			barrier_sync(b, vl);
			continue;
		}
		if (!strcmp(*av, "-cyclic")) {
			barrier_cyclic(b, vl);
			continue;
		}
		vtc_fatal(vl, "Unknown barrier argument: %s", *av);
	}
	AZ(pthread_mutex_unlock(&b->mtx));
}
