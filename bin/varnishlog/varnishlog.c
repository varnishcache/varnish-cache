/*
 * $Id$
 *
 * Log tailer for Varnish
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "shmlog.h"
#include "varnishapi.h"

/*
 * It would be simpler to use sparse array initialization and put it
 * directly in tagnames, but -pedantic gets in the way
 */

static struct tagnames {
	enum shmlogtag	tag;
	const char	*name;
} stagnames[] = {
#define SLTM(foo)	{ SLT_##foo, #foo },
#include "shmlog_tags.h"
#undef SLTM
	{ SLT_ENDMARKER, NULL}
};

static const char *tagnames[256];

static struct shmloghead *loghead;

int
main(int argc, char **argv)
{
	int i;
	unsigned u;
	unsigned char *p, *q;

	loghead = VSL_OpenLog();
	
	for (i = 0; stagnames[i].tag != SLT_ENDMARKER; i++)
		tagnames[stagnames[i].tag] = stagnames[i].name;

	q = NULL;
	while (VSL_NextLog(loghead, &q) != NULL)
		;
	while (1) {
		p = VSL_NextLog(loghead, &q);
		if (p == NULL) {
			fflush(stdout);
			sleep(1);
			continue;
		}
		u = (p[2] << 8) | p[3];
		printf("%02x %3d %4d %-12s <",
		    p[0], p[1], u, tagnames[p[0]]);
		if (p[1] > 0)
			fwrite(p + 4, p[1], 1, stdout);
		printf(">\n");
	}
}
