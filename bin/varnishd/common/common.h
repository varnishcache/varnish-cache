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

#ifdef COMMON_COMMON_H
#error "Multiple includes of common/common.h"
#endif
#define COMMON_COMMON_H

#include <stdarg.h>
#include <stdint.h>

#include <sys/types.h>

#include "miniobj.h"
#include "vas.h"
#include "vcs.h"
#include "vdef.h"
#include "vqueue.h"
#include "vsb.h"

/**********************************************************************/

#if !defined(MAX_THREAD_POOLS)
#  define MAX_THREAD_POOLS 32
#endif

/* Name of transient storage */
#define TRANSIENT_STORAGE	"Transient"

extern pid_t mgt_pid;
#define ASSERT_MGT() do { assert(getpid() == mgt_pid);} while (0)

/* varnishd.c */
extern struct vsb *vident;		// XXX: -> heritage ?

/* Really belongs in mgt.h, but storage_file chokes on both */
void MCH_Fd_Inherit(int fd, const char *what);

#define ARGV_ERR(...)						\
	do {							\
		fprintf(stderr, "Error: " __VA_ARGS__);		\
		fprintf(stderr, "(-? gives usage)\n");		\
		exit(2);					\
	} while (0)

/* cache/cache_acceptor.c */
struct transport;
void XPORT_Init(void);
const struct transport *XPORT_Find(const char *name);

/* cache/cache_vcl.c */
int VCL_TestLoad(const char *);

/* mgt_cli.c */
extern struct VCLS	*mgt_cls;
