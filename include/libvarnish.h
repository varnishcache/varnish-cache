/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
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

#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <sys/types.h>

#include "vas.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

struct vsb;

#include "argv.h"

/* from libvarnish/num.c */
const char *str2bytes(const char *p, uintmax_t *r, uintmax_t rel);

/* from libvarnish/subproc.c */
typedef void sub_func_f(void*);
int SUB_run(struct vsb *sb, sub_func_f *func, void *priv, const char *name,
    int maxlines);

/* from libvarnish/tcp.c */
/* NI_MAXHOST and NI_MAXSERV are ridiculously long for numeric format */
#define TCP_ADDRBUFSIZE		64
#define TCP_PORTBUFSIZE		16

#if (defined (__SVR4) && defined (__sun)) || defined (__NetBSD__)
/*
 * Solaris returns EINVAL if the other end unexepectedly reset the
 * connection.  This is a bug in Solaris and documented behaviour on NetBSD.
 */
#define TCP_Check(a) ((a) == 0 || errno == ECONNRESET || errno == ENOTCONN \
    || errno == EINVAL)
#else
#define TCP_Check(a) ((a) == 0 || errno == ECONNRESET || errno == ENOTCONN)
#endif

#define TCP_Assert(a) assert(TCP_Check(a))

void TCP_myname(int sock, char *abuf, unsigned alen, char *pbuf, unsigned plen);
void TCP_hisname(int sock, char *abuf, unsigned alen, char *pbuf, unsigned plen);
int TCP_filter_http(int sock);
int TCP_blocking(int sock);
int TCP_nonblocking(int sock);
int TCP_linger(int sock, int linger);
#ifdef SOL_SOCKET
int TCP_port(const struct sockaddr_storage *addr);
void TCP_name(const struct sockaddr_storage *addr, unsigned l, char *abuf,
    unsigned alen, char *pbuf, unsigned plen);
int TCP_connect(int s, const struct sockaddr_storage *name, socklen_t namelen,
    int msec);
void TCP_close(int *s);
void TCP_set_read_timeout(int s, double seconds);
#endif

/* from libvarnish/time.c */
#define TIM_FORMAT_SIZE 30
void TIM_format(double t, char *p);
time_t TIM_parse(const char *p);
double TIM_mono(void);
double TIM_real(void);
void TIM_sleep(double t);
struct timespec TIM_timespec(double t);
struct timeval TIM_timeval(double t);

/* from libvarnish/version.c */
void varnish_version(const char *);

/* from libvarnish/vtmpfile.c */
int vtmpfile(char *);
char *vreadfile(const char *pfx, const char *fn, ssize_t *sz);
char *vreadfd(int fd, ssize_t *sz);

const char* vcs_version(void);

/* Safe printf into a fixed-size buffer */
#define bprintf(buf, fmt, ...)						\
	do {								\
		assert(snprintf(buf, sizeof buf, fmt, __VA_ARGS__)	\
		    < sizeof buf);					\
	} while (0)

/* Safe printf into a fixed-size buffer */
#define vbprintf(buf, fmt, ap)						\
	do {								\
		assert(vsnprintf(buf, sizeof buf, fmt, ap)		\
		    < sizeof buf);					\
	} while (0)
