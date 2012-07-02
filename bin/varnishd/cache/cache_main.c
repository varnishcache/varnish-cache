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

#include "waiter/waiter.h"
#include "hash/hash_slinger.h"

volatile struct params	*cache_param;

/*--------------------------------------------------------------------
 * Per thread storage for the session currently being processed by
 * the thread.  This is used for panic messages.
 */

static pthread_key_t req_key;

void
THR_SetRequest(const struct req *req)
{

	AZ(pthread_setspecific(req_key, req));
}

const struct req *
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
 */

static uint32_t vxid_base;
static struct lock vxid_lock;

static void
vxid_More(struct vxid_pool *v)
{

	Lck_Lock(&vxid_lock);
	v->next = vxid_base;
	v->count = 32768;
	vxid_base = v->count;
	Lck_Unlock(&vxid_lock);
}

uint32_t
VXID_Get(struct vxid_pool *v)
{
	if (v->count == 0)
		vxid_More(v);
	AN(v->count);
	v->count--;
	return (v->next++);
}

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
	AZ(pthread_key_create(&name_key, NULL));

	THR_SetName("cache-main");

	VSM_Init();	/* First, LCK needs it. */

	LCK_Init();	/* Second, locking */

	Lck_New(&vxid_lock, lck_vxid);

	WAIT_Init();
	PAN_Init();
	CLI_Init();
	Fetch_Init();

	CNT_Init();
	VCL_Init();

	HTTP_Init();

	VDI_Init();
	VBO_Init();
	VBE_InitCfg();
	VBP_Init();
	WRK_Init();
	Pool_Init();

	EXP_Init();
	HSH_Init(heritage.hash);
	BAN_Init();

	VCA_Init();

	SMS_Init();
	SMP_Init();
	STV_open();

	VMOD_Init();

	BAN_Compile();

	/* Wait for persistent storage to load if asked to */
	if (cache_param->diag_bitmap & 0x00020000)
		SMP_Ready();

	CLI_Run();

	STV_close();

	printf("Child dies\n");
}
