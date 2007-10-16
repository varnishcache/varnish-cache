/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#include "shmlog.h"
#include "cache.h"
#include "heritage.h"
#include "stevedore.h"

/*--------------------------------------------------------------------
 * XXX: Think more about which order we start things
 */

void
child_main(void)
{

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	printf("Child starts\n");

#define SZOF(foo)	printf("sizeof(%s) = %zd\n", #foo, sizeof(foo));
	SZOF(struct ws);
	SZOF(struct http);
	SZOF(struct http_conn);
	SZOF(struct acct);
	SZOF(struct worker);
	SZOF(struct workreq);
	SZOF(struct bereq);
	SZOF(struct storage);
	SZOF(struct object);
	SZOF(struct objhead);
	SZOF(struct sess);
	SZOF(struct vbe_conn);
	SZOF(struct backend);


	CNT_Init();
	VCL_Init();

	HTTP_Init();
	SES_Init();

	VBE_Init();
	VSL_Init();
	WRK_Init();

	EXP_Init();
	HSH_Init();
	BAN_Init();

	STV_open();

	printf("Ready\n");
	VSL_stats->start_time = (time_t)TIM_real();

	CLI_Init();

	printf("Child dies\n");
}
