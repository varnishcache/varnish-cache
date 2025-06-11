/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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

#include <pthread.h>

/* cache_acceptor.c */
struct listen_sock;
struct listen_arg;
struct pool;
struct lock;

void VCA_Init(void);
void VCA_Start(struct cli *cli);
void VCA_Shutdown(void);

enum vca_event {
	VCA_EVENT_LADDR,
};

typedef int acceptor_config_f(void);
typedef void acceptor_init_f(void);
typedef int acceptor_open_f(char **, struct listen_arg *,
    const char **);
typedef int acceptor_reopen_f(void);
typedef void acceptor_start_f(struct cli *);
typedef void acceptor_event_f(struct cli *, struct listen_sock *,
    enum vca_event);
typedef void acceptor_accept_f(struct pool *);
typedef void acceptor_update_f(struct lock *);
typedef void acceptor_shutdown_f(void);

struct acceptor {
	unsigned			magic;
#define ACCEPTOR_MAGIC			0x0611847c
	VTAILQ_ENTRY(acceptor)		list;
	VTAILQ_HEAD(,listen_sock)	socks;
	const char			*name;
	void				*vca_priv;

	acceptor_config_f		*config;
	acceptor_init_f			*init;
	acceptor_open_f			*open;
	acceptor_reopen_f		*reopen;
	acceptor_start_f		*start;
	acceptor_event_f		*event;
	acceptor_accept_f		*accept;
	acceptor_update_f		*update;
	acceptor_shutdown_f		*shutdown;
};

#define VCA_Foreach(arg) for (arg = NULL; VCA__iter(&arg);)
int VCA__iter(struct acceptor ** const pp);

extern struct acceptor TCP_acceptor;
extern struct acceptor UDS_acceptor;
