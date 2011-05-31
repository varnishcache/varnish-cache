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

struct cli;

extern pid_t mgt_pid;
#define ASSERT_MGT() do { assert(getpid() == mgt_pid);} while (0)

/* cache_acceptor.c */
void VCA_tweak_waiter(struct cli *cli, const char *arg);

/* mgt_shmem.c */
extern struct VSC_C_main *VSC_C_main;

/* varnishd.c */
struct vsb;
extern struct vsb *vident;
int Symbol_Lookup(struct vsb *vsb, void *ptr);

#define TRUST_ME(ptr)	((void*)(uintptr_t)(ptr))


/* Help shut up FlexeLint */
#define __match_proto__(xxx) /*lint -e{818} */

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
extern struct VSM_head		*VSM_head;
extern const struct VSM_chunk	*vsm_end;

/*
 * These three should not be called directly, but only through
 * proper vectors in mgt.h/cache.h, hence the __
 */
void *VSM__Alloc(unsigned size, const char *class, const char *type,
    const char *ident);
void VSM__Free(const void *ptr);
void VSM__Clean(void);

/* These classes are opaque to other programs, so we define the here */
#define VSM_CLASS_FREE	"Free"
#define VSM_CLASS_COOL	"Cool"
#define VSM_CLASS_PARAM	"Params"
#define VSM_CLASS_MARK	"MgrCld"
#define VSM_COOL_TIME	5

/* cache_lck.c */
struct lock { void *priv; };		// Opaque

/*---------------------------------------------------------------------
 * Generic power-2 rounding macros
 */

#define PWR2(x)     ((((x)-1)&(x))==0)		/* Is a power of two */
#define RDN2(x, y)  ((x)&(~((y)-1)))		/* if y is powers of two */
#define RUP2(x, y)  (((x)+((y)-1))&(~((y)-1)))	/* if y is powers of two */
