/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 */

struct suckaddr;

/* from libvarnish/tcp.c */
/* NI_MAXHOST and NI_MAXSERV are ridiculously long for numeric format */
#define VTCP_ADDRBUFSIZE		64
#define VTCP_PORTBUFSIZE		16

int VTCP_Check(int a);
#define VTCP_Assert(a) assert(VTCP_Check(a))

struct suckaddr *VTCP_my_suckaddr(int sock);
void VTCP_myname(int sock, char *abuf, unsigned alen,
    char *pbuf, unsigned plen);
void VTCP_hisname(int sock, char *abuf, unsigned alen,
    char *pbuf, unsigned plen);
int VTCP_filter_http(int sock);
int VTCP_fastopen(int sock, int depth);
void VTCP_blocking(int sock);
void VTCP_nonblocking(int sock);
int VTCP_linger(int sock, int linger);
int VTCP_check_hup(int sock);

// #ifdef SOL_SOCKET
void VTCP_name(const struct suckaddr *addr, char *abuf, unsigned alen,
    char *pbuf, unsigned plen);
int VTCP_connected(int s);
int VTCP_connect(const struct suckaddr *name, int msec);
int VTCP_open(const char *addr, const char *def_port, vtim_dur timeout,
    const char **err);
void VTCP_close(int *s);
int VTCP_bind(const struct suckaddr *addr, const char **errp);
int VTCP_listen(const struct suckaddr *addr, int depth, const char **errp);
int VTCP_listen_on(const char *addr, const char *def_port, int depth,
    const char **errp);
void VTCP_set_read_timeout(int s, vtim_dur seconds);
int VTCP_read(int fd, void *ptr, size_t len, vtim_dur tmo);
// #endif
