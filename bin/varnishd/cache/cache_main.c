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
 */

#include "config.h"

#include "cache.h"

#include <stdio.h>
#include <stdlib.h>

#include "common/heritage.h"

#include "vcli_serve.h"
#include "vrnd.h"

#include "hash/hash_slinger.h"


volatile struct params	*cache_param;

/*--------------------------------------------------------------------
 * Per thread storage for the session currently being processed by
 * the thread.  This is used for panic messages.
 */

static pthread_key_t req_key;
static pthread_key_t bo_key;
pthread_key_t witness_key;

void
THR_SetBusyobj(const struct busyobj *bo)
{

	AZ(pthread_setspecific(bo_key, bo));
}

struct busyobj *
THR_GetBusyobj(void)
{

	return (pthread_getspecific(bo_key));
}

void
THR_SetRequest(const struct req *req)
{

	AZ(pthread_setspecific(req_key, req));
}

struct req *
THR_GetRequest(void)
{

	return (pthread_getspecific(req_key));
}

/*--------------------------------------------------------------------
 * Name threads if our pthreads implementation supports it.
 */

static pthread_key_t name_key;

void
THR_SetName(const char *name)
{

	AZ(pthread_setspecific(name_key, name));
#if defined(HAVE_PTHREAD_SET_NAME_NP)
	pthread_set_name_np(pthread_self(), name);
#elif defined(HAVE_PTHREAD_SETNAME_NP)
#if defined(__APPLE__)
	pthread_setname_np(name);
#elif defined(__NetBSD__)
	pthread_setname_np(pthread_self(), "%s", (char *)(uintptr_t)name);
#else
	pthread_setname_np(pthread_self(), name);
#endif
#endif
}

const char *
THR_GetName(void)
{

	return (pthread_getspecific(name_key));
}

/*--------------------------------------------------------------------
 * VXID's are unique transaction numbers allocated with a minimum of
 * locking overhead via pools in the worker threads.
 *
 * VXID's are mostly for use in VSL and for that reason we never return
 * zero vxid, in order to reserve that for "unassociated" VSL records.
 */

static uint32_t vxid_base;
static uint32_t vxid_chunk = 32768;
static struct lock vxid_lock;

uint32_t
VXID_Get(struct worker *wrk, uint32_t mask)
{
	struct vxid_pool *v;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	v = &wrk->vxid_pool;
	AZ(VXID(mask));
	do {
		if (v->count == 0) {
			Lck_Lock(&vxid_lock);
			v->next = vxid_base;
			v->count = vxid_chunk;
			vxid_base = (vxid_base + v->count) & VSL_IDENTMASK;
			Lck_Unlock(&vxid_lock);
		}
		v->count--;
		v->next++;
	} while (v->next == 0);
	return (v->next | mask);
}

/*--------------------------------------------------------------------
 * Debugging aids
 */

/*
 * Dumb down the VXID allocation to make it predictable for
 * varnishtest cases
 */
static void
cli_debug_xid(struct cli *cli, const char * const *av, void *priv)
{
	(void)priv;
	if (av[2] != NULL) {
		vxid_base = strtoul(av[2], NULL, 0);
		vxid_chunk = 1;
	}
	VCLI_Out(cli, "XID is %u", vxid_base);
}

/*
 * Default to seed=1, this is the only seed value POSIXl guarantees will
 * result in a reproducible random number sequence.
 */
static void __match_proto__(cli_func_t)
cli_debug_srandom(struct cli *cli, const char * const *av, void *priv)
{
	unsigned seed = 1;

	(void)priv;
	(void)cli;
	if (av[2] != NULL)
		seed = strtoul(av[2], NULL, 0);
	VRND_SeedTestable(seed);
}

static struct cli_proto debug_cmds[] = {
	{ CLICMD_DEBUG_XID,			"d", cli_debug_xid },
	{ CLICMD_DEBUG_SRANDOM,			"d", cli_debug_srandom },
	{ NULL }
};

/*--------------------------------------------------------------------
 * XXX: Think more about which order we start things
 */

#if defined(__FreeBSD__) && __FreeBSD__version >= 1000000
static void
child_malloc_fail(void *p, const char *s)
{
	VSL(SLT_Error, 0, "MALLOC ERROR: %s (%p)", s, p);
	fprintf(stderr, "MALLOC ERROR: %s (%p)\n", s, p);
	WRONG("Malloc Error");
}
#endif

void
child_main(void)
{

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	printf("Child starts\n");
#if defined(__FreeBSD__) && __FreeBSD__version >= 1000000
	malloc_message = child_malloc_fail;
#endif

	cache_param = heritage.param;

	AZ(pthread_key_create(&req_key, NULL));
	AZ(pthread_key_create(&bo_key, NULL));
	AZ(pthread_key_create(&witness_key, NULL));
	AZ(pthread_key_create(&name_key, NULL));

	THR_SetName("cache-main");

	VSM_Init();	/* First, LCK needs it. */

	LCK_Init();	/* Second, locking */

	Lck_New(&vxid_lock, lck_vxid);

	CLI_Init();
	PAN_Init();
	VFP_Init();

	ObjInit();

	VCL_Init();

	HTTP_Init();

	VBO_Init();
	VBT_Init();
	VBP_Init();
	VBE_InitCfg();
	Pool_Init();
	V1P_Init();
	V2D_Init();

	EXP_Init();
	HSH_Init(heritage.hash);
	BAN_Init();

	VCA_Init();

	STV_open();

	VMOD_Init();

	BAN_Compile();

	VRND_SeedAll();

	CLI_AddFuncs(debug_cmds);

	/* Wait for persistent storage to load if asked to */
	if (FEATURE(FEATURE_WAIT_SILO))
		SMP_Ready();

	CLI_Run();

	VCA_Shutdown();
	BAN_Shutdown();
	STV_close();

	printf("Child dies\n");
}
