/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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

extern pid_t mgt_pid;
#define ASSERT_MGT() do { assert(getpid() == mgt_pid);} while (0)

/* cache_acceptor.c */
void VCA_tweak_waiter(struct cli *cli, const char *arg);

/* mgt_shmem.c */
extern struct varnish_stats *VSL_stats;
extern struct shmloghead *loghead;
extern uint8_t			*vsl_log_start;
extern uint8_t			*vsl_log_end;
extern uint8_t			*vsl_log_nxt;

/* varnishd.c */
struct vsb;
extern struct vsb *vident;
int Symbol_Lookup(struct vsb *vsb, void *ptr);
extern unsigned L_arg;

#define TRUST_ME(ptr)	((void*)(uintptr_t)(ptr))

/* Really belongs in mgt.h, but storage_file chokes on both */
void mgt_child_inherit(int fd, const char *what);

#define ARGV_ERR(...)						\
	do {							\
		fprintf(stderr, "Error: " __VA_ARGS__);		\
		exit(2);					\
	} while (0);

/* A tiny helper for choosing hash/storage modules */
struct choice {
	const char      *name;
	void            *ptr;
};

#define NEEDLESS_RETURN(foo)	return (foo)

/**********************************************************************
 * Guess what:  There is no POSIX standard for memory barriers.
 * XXX: Please try to find the minimal #ifdef to use here, rely on OS
 * supplied facilities if at all possible, to avoid descending into the
 * full cpu/compiler explosion.
 */

#ifdef __FreeBSD__
#include <machine/atomic.h>
#define MEMORY_BARRIER()       mb()
#else
#warn "MEMORY_BARRIER() is expensive"
#define MEMORY_BARRIER()       close(-1)
#endif
