/*
 * $Id$
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#include "heritage.h"
#include "shmlog.h"
#include "cache.h"

struct stevedore	*stevedore;

/*--------------------------------------------------------------------
 * XXX: Think more about which order we start things
 */

void
child_main(void)
{

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	printf("Child starts\n");

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

	stevedore = heritage.stevedore;
	if (stevedore->open != NULL)
		stevedore->open(stevedore);

	printf("Ready\n");
	VSL_stats->start_time = time(NULL);

	CLI_Init();

	printf("Child dies\n");
}

