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

#include <errno.h>
#include <stdint.h>

#include <sys/types.h>

#include "miniobj.h"
#include "vas.h"
#include "vcs.h"
#include "vdef.h"
#include "vqueue.h"
#include "vsb.h"

struct cli;

/* Name of transient storage */
#define TRANSIENT_STORAGE	"Transient"

extern pid_t mgt_pid;
#define ASSERT_MGT() do { assert(getpid() == mgt_pid);} while (0)

/* varnishd.c */
extern struct vsb *vident;		// XXX: -> heritage ?
int Symbol_Lookup(struct vsb *vsb, void *ptr);

/* Help shut up FlexeLint */
#define __match_proto__(xxx) /*lint -e{818} */

/* Really belongs in mgt.h, but storage_file chokes on both */
void mgt_child_inherit(int fd, const char *what);

#define ARGV_ERR(...)						\
	do {							\
		fprintf(stderr, "Error: " __VA_ARGS__);		\
		exit(2);					\
	} while (0);

#define NEEDLESS_RETURN(foo)	return (foo)


/* vsm.c */
struct vsm_sc;
struct vsm_sc *VSM_common_new(void *ptr, ssize_t len);
void *VSM_common_alloc(struct vsm_sc *sc, ssize_t size,
    const char *class, const char *type, const char *ident);
void VSM_common_free(struct vsm_sc *sc, void *ptr);
void VSM_common_delete(struct vsm_sc **sc);
void VSM_common_copy(struct vsm_sc *to, const struct vsm_sc *from);

/*---------------------------------------------------------------------
 * Generic power-2 rounding macros
 */

#define PWR2(x)     ((((x)-1)&(x))==0)		/* Is a power of two */
#define RDN2(x, y)  ((x)&(~((y)-1)))		/* if y is powers of two */
#define RUP2(x, y)  (((x)+((y)-1))&(~((y)-1)))	/* if y is powers of two */

/*--------------------------------------------------------------------
 * Pointer aligment magic
 */

#define PALGN	    (sizeof(void *) - 1)	/* size of alignment */
#define PAOK(p)	    (((uintptr_t)(p) & PALGN) == 0)	/* is aligned */
#define PRNDDN(p)   ((uintptr_t)(p) & ~PALGN)		/* Round down */
#define PRNDUP(p)   (((uintptr_t)(p) + PALGN) & ~PALGN)	/* Round up */

/*--------------------------------------------------------------------
 * To be used as little as possible to wash off const/volatile etc.
 */
#define TRUST_ME(ptr)	((void*)(uintptr_t)(ptr))
