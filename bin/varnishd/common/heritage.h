/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 * This file contains the heritage passed when mgt forks cache
 */

struct vsmw;
struct suckaddr;
struct listen_sock;
struct transport;
struct VCLS;
struct uds_perms;
struct conn_heritage;

struct listen_sock {
	unsigned			magic;
#define LISTEN_SOCK_MAGIC		0x999e4b57
	VTAILQ_ENTRY(listen_sock)	list;
	VTAILQ_ENTRY(listen_sock)	arglist;
	int				sock;
	int				uds;
	uint64_t			*nonce;
	char				*endpoint;
	const char			*name;
	const struct suckaddr		*addr;
	const struct transport		*transport;
	const struct uds_perms		*perms;
	unsigned			test_heritage;
	struct conn_heritage		*conn_heritage;
};

VTAILQ_HEAD(listen_sock_head, listen_sock);

struct heritage {

	/* Two pipe(2)'s for CLI connection between cache and mgt.  */
	int				cli_in;
	int				cli_out;

	/* File descriptor for stdout/stderr */
	int				std_fd;

	/* File descriptor for smuggling socketpair */
	int				fence;

	int				vsm_fd;

	/* Sockets from which to accept connections */
	struct listen_sock_head		socks;

	/* Hash method */
	const struct hash_slinger	*hash;

	struct params			*param;

	const char			*identity;

	char				*panic_str;
	ssize_t				panic_str_len;

	struct VCLS			*cls;

	const char			*ident;

	long				mgt_pid;

	struct vsmw			*proc_vsmw;

	unsigned			min_vcl_version;
	unsigned			max_vcl_version;

	int				argc;
	char * const *			argv;
};

extern struct heritage heritage;

#define ASSERT_MGT() do { assert(getpid() == heritage.mgt_pid);} while (0)

/* Really belongs in mgt.h, but storage_file chokes on both */
void MCH_Fd_Inherit(int fd, const char *what);

#define ARGV_ERR(...)						\
	do {							\
		fprintf(stderr, "Error: " __VA_ARGS__);		\
		fprintf(stderr, "(-? gives usage)\n");		\
		exit(2);					\
	} while (0)

/* cache/cache_main.c */
void child_main(int, size_t);

/* cache/cache_vcl.c */
int VCL_TestLoad(const char *);

/* cache/cache_acceptor.c */
struct transport;
void XPORT_Init(void);
const struct transport *XPORT_Find(const char *name);

/* common/common_vsc.c & common/common_vsmw.c */
typedef void vsm_lock_f(void);
extern vsm_lock_f *vsc_lock;
extern vsm_lock_f *vsc_unlock;
extern vsm_lock_f *vsmw_lock;
extern vsm_lock_f *vsmw_unlock;

/* common/common_vext.c */

void vext_argument(const char *);
void vext_copyin(struct vsb *);
void vext_load(void);
void vext_cleanup(int);
typedef void vext_iter_f(const char *, void *);
void vext_iter(vext_iter_f *func, void *);
