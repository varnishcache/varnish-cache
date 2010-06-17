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
extern struct vsc_main *VSC_main;

/* varnishd.c */
struct vsb;
extern struct vsb *vident;
int Symbol_Lookup(struct vsb *vsb, void *ptr);

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
	const void	*ptr;
};
const void *pick(const struct choice *cp, const char *which, const char *kind);

#define NEEDLESS_RETURN(foo)	return (foo)

/* vsm.c */
extern struct vsm_head		*vsm_head;
extern const struct vsm_chunk	*vsm_end;

void *VSM_Alloc(unsigned size, const char *class, const char *type,
    const char *ident);
void VSM_Free(const void *ptr);
void VSM_Clean(void);


struct vsm_chunk *vsm_iter_0(void);
void vsm_iter_n(struct vsm_chunk **pp);

#define VSM_ITER(vd) for ((vd) = vsm_iter_0(); (vd) != NULL; vsm_iter_n(&vd))

/* These classes are opaque to other programs, so we define the here */
#define VSM_CLASS_FREE	"Free"
#define VSM_CLASS_COOL	"Cool"
#define VSM_CLASS_PARAM	"Params"
#define VSM_CLASS_MARK	"MgrCld"
#define VSM_COOL_TIME	5
