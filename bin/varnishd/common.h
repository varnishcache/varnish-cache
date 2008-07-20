/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 *
 * $Id$
 */

struct cli;
struct sockaddr;

/* cache_acceptor.c */
void VCA_tweak_acceptor(struct cli *cli, const char *arg);

/* shmlog.c */
void VSL_Panic(int *len, char **ptr);

/* shmlog.c */
void VSL_MgtInit(const char *fn, unsigned size);
extern struct varnish_stats *VSL_stats;

/* tcp.c */
/* NI_MAXHOST and NI_MAXSERV are ridiculously long for numeric format */
#define TCP_ADDRBUFSIZE		64
#define TCP_PORTBUFSIZE		16

void TCP_name(const struct sockaddr *addr, unsigned l, char *abuf, unsigned alen, char *pbuf, unsigned plen);
void TCP_myname(int sock, char *abuf, unsigned alen, char *pbuf, unsigned plen);
int TCP_filter_http(int sock);
void TCP_blocking(int sock);
void TCP_nonblocking(int sock);
#ifdef SOL_SOCKET
int TCP_connect(int s, const struct sockaddr *name, socklen_t namelen, int msec);
#endif

#define TRUST_ME(ptr)	((void*)(uintptr_t)(ptr))

/* Really belongs in mgt.h, but storage_file chokes on both */
void mgt_child_inherit(int fd, const char *what);

#define ARGV_ERR(...)						\
	do {							\
		fprintf(stderr, "Error: " __VA_ARGS__);		\
		exit(2);					\
	} while (0);
