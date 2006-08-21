/*
 * $Id$
 *
 * Log tailer for Varnish
 */

#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "compat/vis.h"

#include "vsb.h"

#include "libvarnish.h"
#include "shmlog.h"
#include "varnishapi.h"

static int	bflag, cflag;

/* -------------------------------------------------------------------*/

static int
name2tag(const char *n)
{
	int i;

	for (i = 0; i < 256; i++) {
		if (VSL_tags[i] == NULL)
			continue;
		if (!strcasecmp(n, VSL_tags[i]))
			return (i);
	}
	return (-1);
}

/* Ordering-----------------------------------------------------------*/

static struct vsb	*ob[65536];
static unsigned char	flg[65536];
#define F_INVCL		(1 << 0)
#define F_MATCH		(1 << 1)

static int		match_tag = -1;
static regex_t		match_re;

static void
clean_order(void)
{
	unsigned u;

printf("Clean\n");
	for (u = 0; u < 65536; u++) {
		if (ob[u] == NULL)
			continue;
		vsb_finish(ob[u]);
		if (vsb_len(ob[u]) > 1 &&
		    (match_tag == -1 || flg[u] & F_MATCH))
			printf("%s\n", vsb_data(ob[u]));
		flg[u] = 0;
		vsb_clear(ob[u]);
	}
}

static int 
h_order(void *priv, unsigned tag, unsigned fd, unsigned len, unsigned spec, const char *ptr)
{

	(void)priv;

	if (!(spec & (VSL_S_CLIENT|VSL_S_BACKEND))) {
		if (!bflag && !cflag)
			VSL_H_Print(stdout, tag, fd, len, spec, ptr);
		return (0);
	}
	if (ob[fd] == NULL) {
		ob[fd] = vsb_new(NULL, NULL, 0, VSB_AUTOEXTEND);
		assert(ob[fd] != NULL);
	}
	if (tag == match_tag &&
	    !regexec(&match_re, ptr, 0, NULL, 0))
		flg[fd] |= F_MATCH;
	switch (tag) {
	case SLT_VCL_call:
		flg[fd] |= F_INVCL;
		vsb_printf(ob[fd], "%5d %-12s %c %.*s",
		    fd, VSL_tags[tag],
		    ((spec & VSL_S_CLIENT) ? 'c' : \
		    (spec & VSL_S_BACKEND) ? 'b' : '-'),
		    len, ptr);
		return (0);
	case SLT_VCL_trace:
	case SLT_VCL_return:
		if (flg[fd] & F_INVCL) {
			vsb_cat(ob[fd], " ");
			vsb_bcat(ob[fd], ptr, len);
			return (0);
		}
		break;
	default:
		break;
	}
	if (flg[fd] & F_INVCL) {
		vsb_cat(ob[fd], "\n");
		flg[fd] &= ~F_INVCL;
	}
	vsb_printf(ob[fd], "%5d %-12s %c %.*s\n",
	    fd, VSL_tags[tag],
	    ((spec & VSL_S_CLIENT) ? 'c' : (spec & VSL_S_BACKEND) ? 'b' : '-'),
	    len, ptr);
	switch (tag) {
	case SLT_ReqEnd:
	case SLT_BackendClose:
	case SLT_BackendReuse:
	case SLT_StatSess:
		vsb_finish(ob[fd]);
		if (vsb_len(ob[fd]) > 1 &&
		    (match_tag == -1 || flg[fd] & F_MATCH))
			printf("%s\n", vsb_data(ob[fd]));
		flg[fd] &= ~F_MATCH;
		vsb_clear(ob[fd]);
		break;
	default:
		break;
	}
	return (0);
}

static void
do_order(struct VSL_data *vd, int argc, char **argv)
{
	int i;

	if (argc == 2) {
		match_tag = name2tag(argv[0]);
		if (match_tag < 0) {
			fprintf(stderr, "Tag \"%s\" unknown\n", argv[0]);
			exit (2);
		}
		i = regcomp(&match_re, argv[1], REG_EXTENDED | REG_NOSUB);
		if (i) {
			char buf[BUFSIZ];
			regerror(i, &match_re, buf, sizeof buf);
			fprintf(stderr, "%s\n", buf);
			exit (2);
		}
	}
	if (!bflag) {
		VSL_Select(vd, SLT_SessionOpen);
		VSL_Select(vd, SLT_SessionClose);
		VSL_Select(vd, SLT_ReqEnd);
	}
	if (!cflag) {
		VSL_Select(vd, SLT_BackendOpen);
		VSL_Select(vd, SLT_BackendClose);
		VSL_Select(vd, SLT_BackendReuse);
	}
	while (1) {
		i = VSL_Dispatch(vd, h_order, NULL);
		if (i == 0) {
			clean_order();
			fflush(stdout);
		}
		else if (i < 0)
			break;
	} 
	clean_order();
}

/*--------------------------------------------------------------------*/

static void
do_write(struct VSL_data *vd, const char *w_opt)
{
	int fd, i;
	unsigned char *p;

	if (!strcmp(w_opt, "-"))
		fd = STDOUT_FILENO;
	else
		fd = open(w_opt, O_WRONLY|O_APPEND|O_CREAT, 0644);
	if (fd < 0) {
		perror(w_opt);
		exit (1);
	}
	while (1) {
		i = VSL_NextLog(vd, &p);
		if (i < 0)
			break;
		if (i > 0) {
			i = write(fd, p, 5 + p[1]);
			if (i != 1)
				perror(w_opt);
		}
	}
	exit (0);
}

/*--------------------------------------------------------------------*/

static void
usage(void)
{
	fprintf(stderr,
	    "usage: varnishlog [(stdopts)] [-oV] [-w file] [-r file]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int i, c;
	int o_flag = 0;
	char *w_opt = NULL;
	struct VSL_data *vd;

	vd = VSL_New();
	
	while ((c = getopt(argc, argv, VSL_ARGS "oVw:")) != -1) {
		switch (c) {
		case 'o':
			o_flag = 1;
			break;
		case 'V':
			varnish_version("varnishlog");
			exit(0);
		case 'w':
			w_opt = optarg;
			break;
		case 'c':
			cflag = 1;
			if (VSL_Arg(vd, c, optarg) > 0)
				break;
			usage();
		case 'b':
			bflag = 1;
			if (VSL_Arg(vd, c, optarg) > 0)
				break;
			usage();
		default:
			if (VSL_Arg(vd, c, optarg) > 0)
				break;
			usage();
		}
	}

	if (o_flag && w_opt != NULL)
		usage();

	if (VSL_OpenLog(vd))
		exit (1);

	if (w_opt != NULL) 
		do_write(vd, w_opt);

	if (o_flag)
		do_order(vd, argc - optind, argv + optind);

	while (1) {
		i = VSL_Dispatch(vd, VSL_H_Print, stdout);
		if (i == 0)
			fflush(stdout);
		else if (i < 0)
			break;
	} 

	return (0);
}
