/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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

#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#include "shmlog.h"
#include "cache.h"
#include "stevedore.h"

/*--------------------------------------------------------------------
 * Per thread storage for the session currently being processed by
 * the thread.  This is used for panic messages.
 */

static pthread_key_t sp_key;

void
THR_SetSession(const struct sess *sp)
{

	AZ(pthread_setspecific(sp_key, sp));
}

const struct sess *
THR_GetSession(void)
{

	return (pthread_getspecific(sp_key));
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
 * XXX: Think more about which order we start things
 */

void
child_main(void)
{

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	printf("Child starts\n");

	AZ(pthread_key_create(&sp_key, NULL));
	AZ(pthread_key_create(&name_key, NULL));

	THR_SetName("cache-main");

	VSL_Init();	/* First, LCK needs it. */

	LCK_Init();	/* Locking, must be first */

	PAN_Init();
	CLI_Init();
	Fetch_Init();

	CNT_Init();
	VCL_Init();

	HTTP_Init();
	SES_Init();

	VBE_Init();
	VBP_Init();
	WRK_Init();

	EXP_Init();
	HSH_Init();
	BAN_Init();

	VCA_Init();

	SMS_Init();
	STV_open();

	VSL_stats->start_time = (time_t)TIM_real();

	CLI_Run();

	printf("Child dies\n");
}
