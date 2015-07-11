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

#include <stdint.h>

#include <sys/types.h>

#include "miniobj.h"
#include "vas.h"
#include "vcs.h"
#include "vdef.h"
#include "vqueue.h"
#include "vsb.h"
#include "vapi/vsc_int.h"

/*
 * Enums cannot be forward declared (any more...) so put it here
 * to make everybody see it.
 */

enum baninfo {
	BI_NEW,
	BI_DROP
};

enum obj_attr {
#define OBJ_ATTR(U, l)	OA_##U,
#include "tbl/obj_attr.h"
#undef OBJ_ATTR
};

enum obj_flags {
#define OBJ_FLAG(U, l, v)	OF_##U = v,
#include "tbl/obj_attr.h"
#undef OBJ_FLAG
};

enum sess_step {
#define SESS_STEP(l, u)		S_STP_##u,
#include "tbl/steps.h"
#undef SESS_STEP
};

struct cli;

/**********************************************************************
 * FlexeLint and compiler shutuppery
 */

/*
 * State variables may change value before we use the last value we
 * set them to.
 * Pass no argument.
 */
#define __state_variable__(xxx)		/*lint -esym(838,xxx) */

/**********************************************************************/

/* Name of transient storage */
#define TRANSIENT_STORAGE	"Transient"

extern pid_t mgt_pid;
#define ASSERT_MGT() do { assert(getpid() == mgt_pid);} while (0)

/* varnishd.c */
extern struct vsb *vident;		// XXX: -> heritage ?
int Symbol_Lookup(struct vsb *vsb, void *ptr);


/* Really belongs in mgt.h, but storage_file chokes on both */
void mgt_child_inherit(int fd, const char *what);

#define ARGV_ERR(...)						\
	do {							\
		fprintf(stderr, "Error: " __VA_ARGS__);		\
		exit(2);					\
	} while (0)

/* cache/cache_vcl.c */
int VCL_TestLoad(const char *);

/* vsm.c */
struct vsm_sc;
struct VSC_C_main;
struct vsm_sc *VSM_common_new(void *ptr, ssize_t len);
void *VSM_common_alloc(struct vsm_sc *sc, ssize_t size,
    const char *class, const char *type, const char *ident);
void VSM_common_free(struct vsm_sc *sc, void *ptr);
void VSM_common_delete(struct vsm_sc **sc);
void VSM_common_copy(struct vsm_sc *to, const struct vsm_sc *from);
void VSM_common_cleaner(struct vsm_sc *sc, struct VSC_C_main *stats);
void VSM_common_ageupdate(const struct vsm_sc *sc);

/*---------------------------------------------------------------------
 * Generic power-2 rounding macros
 */

#define PWR2(x)     ((((x)-1UL)&(x))==0)		/* Is a power of two */
#define RDN2(x, y)  ((x)&(~((uintptr_t)(y)-1UL)))	/* PWR2(y) true */
#define RUP2(x, y)  (((x)+((y)-1))&(~((uintptr_t)(y)-1UL))) /* PWR2(y) true */

/*--------------------------------------------------------------------
 * Pointer aligment magic
 */

#if defined(__sparc__)
/* NB: Overbroad test for 32bit userland on 64bit SPARC cpus. */
#  define PALGN	    (sizeof(double) - 1)	/* size of alignment */
#else
#  define PALGN	    (sizeof(void *) - 1)	/* size of alignment */
#endif
#define PAOK(p)	    (((uintptr_t)(p) & PALGN) == 0)	/* is aligned */
#define PRNDDN(p)   ((uintptr_t)(p) & ~PALGN)		/* Round down */
#define PRNDUP(p)   (((uintptr_t)(p) + PALGN) & ~PALGN)	/* Round up */

/*--------------------------------------------------------------------
 * To be used as little as possible to wash off const/volatile etc.
 */
#define TRUST_ME(ptr)	((void*)(uintptr_t)(ptr))
