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
#include <assert.h>
#include <sbuf.h>

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

/* Ordering-----------------------------------------------------------*/

static struct sbuf	*ob[65536];

static void 
order(unsigned char *p)
{
	unsigned u;

	u = (p[2] << 8) | p[3];
	if (ob[u] == NULL) {
		ob[u] = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
		assert(ob[u] != NULL);
	}
	switch (p[0]) {
	case SLT_VCL_call:
		sbuf_printf(ob[u], "%02x %3d %4d %-12s",
		    p[0], p[1], u, tagnames[p[0]]);
		if (p[1] > 0) {
			sbuf_cat(ob[u], " <");
			sbuf_bcat(ob[u], p + 4, p[1]);
		}
		break;
	case SLT_VCL_trace:
		if (p[1] > 0) {
			sbuf_cat(ob[u], " ");
			sbuf_bcat(ob[u], p + 4, p[1]);
		}
		break;
	case SLT_VCL_return:
		if (p[1] > 0) {
			sbuf_cat(ob[u], " ");
			sbuf_bcat(ob[u], p + 4, p[1]);
			sbuf_cat(ob[u], ">\n");
		}
		break;
	default:
		sbuf_printf(ob[u], "%02x %3d %4d %-12s",
		    p[0], p[1], u, tagnames[p[0]]);
		if (p[1] > 0) {
			sbuf_cat(ob[u], " <");
			sbuf_bcat(ob[u], p + 4, p[1]);
			sbuf_cat(ob[u], ">");
		}
		sbuf_cat(ob[u], "\n");
		break;
	}
	if (u == 0) {
		sbuf_finish(ob[u]);
		printf("%s", sbuf_data(ob[u]));
		sbuf_clear(ob[u]);
		return;
	}
	switch (p[0]) {
	case SLT_SessionClose:
	case SLT_SessionReuse:
	case SLT_BackendClose:
		sbuf_finish(ob[u]);
		printf("%s\n", sbuf_data(ob[u]));
		sbuf_clear(ob[u]);
		break;
	default:
		break;
	}
}



/*--------------------------------------------------------------------*/


int
main(int argc, char **argv)
{
	int i, c;
	unsigned u;
	unsigned char *p, *q;
	int o_flag = 0;
	struct shmloghead *loghead;

	loghead = VSL_OpenLog();
	
	for (i = 0; stagnames[i].tag != SLT_ENDMARKER; i++)
		tagnames[stagnames[i].tag] = stagnames[i].name;

	while ((c = getopt(argc, argv, "o")) != -1) {
		switch (c) {
		case 'o':
			o_flag = 1;
			break;
		default:
			fprintf(stderr, "Usage: varnishlog [-o]\n");
			exit (2);
		}
	}

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
		if (o_flag) {
			order(p);
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
