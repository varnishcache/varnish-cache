/*-
 * Copyright (c) 2020 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 *
 */

#include "config.h"

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "vtc.h"
#include "vtc_http.h"

struct thread_arg {
	unsigned		magic;
#define THREAD_ARG_MAGIC	0xd5dc5f1c
	void			*priv;
	sess_conn_f		*conn_f;
	sess_disc_f		*disc_f;
	const char		*listen_addr;
	struct vtc_sess		*vsp;
	int			*asocket;
	const char		*spec;
};

struct vtc_sess *
Sess_New(struct vtclog *vl, const char *name)
{
	struct vtc_sess *vsp;

	ALLOC_OBJ(vsp, VTC_SESS_MAGIC);
	AN(vsp);
	vsp->vl = vl;
	REPLACE(vsp->name, name);
	vsp->repeat = 1;
	return (vsp);
}

void
Sess_Destroy(struct vtc_sess **vspp)
{
	struct vtc_sess *vsp;

	TAKE_OBJ_NOTNULL(vsp, vspp, VTC_SESS_MAGIC);
	REPLACE(vsp->name, NULL);
	FREE_OBJ(vsp);
}

int
Sess_GetOpt(struct vtc_sess *vsp, char * const **avp)
{
	char * const *av;
	int rv = 0;

	CHECK_OBJ_NOTNULL(vsp, VTC_SESS_MAGIC);
	AN(avp);
	av = *avp;
	AN(*av);
	if (!strcmp(*av, "-rcvbuf")) {
		AN(av[1]);
		vsp->rcvbuf = atoi(av[1]);
		av += 1;
		rv = 1;
	} else if (!strcmp(*av, "-repeat")) {
		AN(av[1]);
		vsp->repeat = atoi(av[1]);
		av += 1;
		rv = 1;
	} else if (!strcmp(*av, "-keepalive")) {
		vsp->keepalive = 1;
		rv = 1;
	}
	*avp = av;
	return (rv);
}

int
sess_process(struct vtclog *vl, struct vtc_sess *vsp,
    const char *spec, int sock, int *sfd, const char *addr)
{
	int rv;

	CHECK_OBJ_NOTNULL(vsp, VTC_SESS_MAGIC);

	rv = http_process(vl, vsp, spec, sock, sfd, addr, vsp->rcvbuf);
	return (rv);
}

static void *
sess_thread(void *priv)
{
	struct vtclog *vl;
	struct vtc_sess *vsp;
	struct thread_arg ta, *tap;
	int i, fd = -1;

	CAST_OBJ_NOTNULL(tap, priv, THREAD_ARG_MAGIC);
	ta = *tap;
	FREE_OBJ(tap);

	vsp = ta.vsp;
	CHECK_OBJ_NOTNULL(vsp, VTC_SESS_MAGIC);
	vl = vtc_logopen("%s", vsp->name);
	pthread_cleanup_push(vtc_logclose, vl);

	assert(vsp->repeat > 0);
	vtc_log(vl, 2, "Started on %s (%u iterations%s)", ta.listen_addr,
		vsp->repeat, vsp->keepalive ? " using keepalive" : "");
	for (i = 0; i < vsp->repeat; i++) {
		if (fd < 0)
			fd = ta.conn_f(ta.priv, vl);
		fd = sess_process(vl, ta.vsp, ta.spec, fd,
		    ta.asocket, ta.listen_addr);
		if (! vsp->keepalive)
			ta.disc_f(ta.priv, vl, &fd);
	}
	if (vsp->keepalive)
		ta.disc_f(ta.priv, vl, &fd);
	vtc_log(vl, 2, "Ending");
	pthread_cleanup_pop(0);
	vtc_logclose(vl);
	return (NULL);
}

pthread_t
Sess_Start_Thread(
    void *priv,
    struct vtc_sess *vsp,
    sess_conn_f *conn,
    sess_disc_f *disc,
    const char *listen_addr,
    int *asocket,
    const char *spec
)
{
	struct thread_arg *ta;
	pthread_t pt;

	AN(priv);
	CHECK_OBJ_NOTNULL(vsp, VTC_SESS_MAGIC);
	AN(conn);
	AN(disc);
	AN(listen_addr);
	ALLOC_OBJ(ta, THREAD_ARG_MAGIC);
	AN(ta);
	ta->priv = priv;
	ta->vsp = vsp;

	ta->conn_f = conn;
	ta->disc_f = disc;
	ta->listen_addr = listen_addr;
	ta->asocket = asocket;
	ta->spec = spec;
	PTOK(pthread_create(&pt, NULL, sess_thread, ta));
	return (pt);
}
