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

#include <stdio.h>
#include <stdlib.h>

#include "cache.h"
#include "common/heritage.h"

#include "vcli_priv.h"
#include "vrnd.h"

#include "waiter/waiter.h"
#include "hash/hash_slinger.h"


volatile struct params	*cache_param;

/*--------------------------------------------------------------------
 * Per thread storage for the session currently being processed by
 * the thread.  This is used for panic messages.
 */

static pthread_key_t req_key;
static pthread_key_t bo_key;

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
#ifdef HAVE_PTHREAD_SET_NAME_NP
	pthread_set_name_np(pthread_self(), name);
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
VXID_Get(struct vxid_pool *v)
{
	do {
		if (v->count == 0) {
			Lck_Lock(&vxid_lock);
			v->next = vxid_base;
			v->count = vxid_chunk;
			vxid_base += v->count;
			Lck_Unlock(&vxid_lock);
		}
		v->count--;
		v->next++;
	} while (v->next == 0);
	return (v->next);
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
static void
cli_debug_srandom(struct cli *cli, const char * const *av, void *priv)
{
	(void)priv;
	unsigned seed = 1;

	if (av[2] != NULL)
		seed = strtoul(av[2], NULL, 0);
	srandom(seed);
	srand48(random());
	VCLI_Out(cli, "Random(3) seeded with %u", seed);
}

static struct cli_proto debug_cmds[] = {
	{ "debug.xid", "debug.xid",
		"\tExamine or set XID\n", 0, 1, "d", cli_debug_xid },
	{ "debug.srandom", "debug.srandom",
		"\tSeed the random(3) function\n", 0, 1, "d",
		cli_debug_srandom },
	{ NULL }
};


/*--------------------------------------------------------------------
 * XXX: Think more about which order we start things
 */

void
child_main(void)
{

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	printf("Child starts\n");

	cache_param = heritage.param;

	AZ(pthread_key_create(&req_key, NULL));
	AZ(pthread_key_create(&bo_key, NULL));
	AZ(pthread_key_create(&name_key, NULL));

	THR_SetName("cache-main");

	VSM_Init();	/* First, LCK needs it. */

	LCK_Init();	/* Second, locking */

	Lck_New(&vxid_lock, lck_vxid);

	WAIT_Init();
	PAN_Init();
	CLI_Init();
	VFP_Init();

	VCL_Init();

	HTTP_Init();

	VDI_Init();
	VBO_Init();
	VBE_InitCfg();
	VBP_Init();
	WRK_Init();
	Pool_Init();
	Pipe_Init();

	EXP_Init();
	HSH_Init(heritage.hash);
	BAN_Init();

	VCA_Init();

	SMP_Init();
	STV_open();

	VMOD_Init();

	BAN_Compile();

	VRND_Seed();
	srand48(random());
	CLI_AddFuncs(debug_cmds);

	/* Wait for persistent storage to load if asked to */
	if (FEATURE(FEATURE_WAIT_SILO))
		SMP_Ready();

	Pool_Accept();

	CLI_Run();

	BAN_Shutdown();
	STV_close();

	printf("Child dies\n");
}
