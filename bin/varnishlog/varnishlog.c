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
clean_order()
{
	unsigned u;

	for (u = 0; u < 65536; u++) {
		if (ob[u] == NULL)
			continue;
		sbuf_finish(ob[u]);
		if (sbuf_len(ob[u]))
			printf("%s\n", sbuf_data(ob[u]));
	}
}

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

static void
Usage(void)
{
	fprintf(stderr, "Usage: varnishlog [-o] [-w file] [-r file]\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	int i, c;
	unsigned u, v;
	unsigned char *p, *q;
	int o_flag = 0;
	char *w_opt = NULL;
	FILE *wfile = NULL;
	char *r_opt = NULL;
	FILE *rfile = NULL;
	unsigned char rbuf[255+4];
	struct shmloghead *loghead;

	loghead = VSL_OpenLog();
	
	for (i = 0; stagnames[i].tag != SLT_ENDMARKER; i++)
		tagnames[stagnames[i].tag] = stagnames[i].name;

	while ((c = getopt(argc, argv, "or:w:")) != -1) {
		switch (c) {
		case 'o':
			o_flag = 1;
			break;
		case 'r':
			r_opt = optarg;
			break;
		case 'w':
			w_opt = optarg;
			break;
		default:
			Usage();
		}
	}

	if (r_opt != NULL && w_opt != NULL)
		Usage();
	if (o_flag && w_opt != NULL)
		Usage();

	if (r_opt != NULL) {
		if (!strcmp(r_opt, "-"))
			rfile = stdin;
		else
			rfile = fopen(r_opt, "r");
		if (rfile == NULL)
			perror(r_opt);
		u = 0;
	}
	if (w_opt != NULL) {
		wfile = fopen(w_opt, "w");
		if (wfile == NULL)
			perror(w_opt);
		u = 0;
	}

	q = NULL;
	if (r_opt == NULL) {
		while (VSL_NextLog(loghead, &q) != NULL)
			;
	}
	while (1) {
		if (r_opt == NULL) {
			p = VSL_NextLog(loghead, &q);
			if (p == NULL) {
				if (w_opt == NULL)
					fflush(stdout);
				else if (++v == 100) {
					fflush(wfile);
					printf("\nFlushed\n");
				}
				usleep(50000);
				continue;
			}
		} else {
			i = fread(rbuf, 4, 1, rfile);
			if (i != 1)
				break;
			if (rbuf[1] > 0)
			i = fread(rbuf + 4, rbuf[1], 1, rfile);
			if (i != 1)
				break;
			p = rbuf;
		}
		if (wfile != NULL) {
			v = 0;
			i = fwrite(p, 4 + p[1], 1, wfile);
			if (i != 1)
				perror(w_opt);
			u++;
			if (!(u % 1000)) {
				printf("%u\r", u);
				fflush(stdout);
			}
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
	if (o_flag)
		clean_order();
}
