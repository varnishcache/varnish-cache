/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
 * Log tailer for Varnish
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "compat/daemon.h"

#include "vsb.h"
#include "vpf.h"

#include "libvarnish.h"
#include "shmlog.h"
#include "vre.h"
#include "varnishapi.h"

static int	b_flag, c_flag;

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
static enum shmlogtag   last[65536];
#define F_INVCL		(1 << 0)
#define F_MATCH		(1 << 1)

static int		match_tag = -1;
static vre_t		*match_re;

static void
h_order_finish(int fd)
{

	vsb_finish(ob[fd]);
	if (vsb_len(ob[fd]) > 1 &&
	    (match_tag == -1 || flg[fd] & F_MATCH))
		printf("%s\n", vsb_data(ob[fd]));
	flg[fd] &= ~F_MATCH;
	vsb_clear(ob[fd]);
}

static void
clean_order(void)
{
	unsigned u;

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
h_order(void *priv, enum shmlogtag tag, unsigned fd, unsigned len,
    unsigned spec, const char *ptr)
{
	char type;

	(void)priv;

	type = (spec & VSL_S_CLIENT) ? 'c' :
	    (spec & VSL_S_BACKEND) ? 'b' : '-';

	if (!(spec & (VSL_S_CLIENT|VSL_S_BACKEND))) {
		if (!b_flag && !c_flag)
			VSL_H_Print(stdout, tag, fd, len, spec, ptr);
		return (0);
	}
	if (ob[fd] == NULL) {
		ob[fd] = vsb_newauto();
		assert(ob[fd] != NULL);
	}
	if (tag == match_tag &&
	    VRE_exec(match_re, ptr, len, 0, 0, NULL, 0) > 0)
		flg[fd] |= F_MATCH;

	if ((tag == SLT_BackendOpen || tag == SLT_SessionOpen ||
		(tag == SLT_ReqStart &&
		    last[fd] != SLT_SessionOpen &&
		    last[fd] != SLT_VCL_acl) ||
		(tag == SLT_BackendXID &&
		    last[fd] != SLT_BackendOpen)) &&
	    vsb_len(ob[fd]) != 0) {
		/*
		 * This is the start of a new request, yet we haven't seen
		 * the end of the previous one.  Spit it out anyway before
		 * starting on the new one.
		 */
		if (last[fd] != SLT_SessionClose)
			vsb_printf(ob[fd], "%5d %-12s %c %s\n",
			    fd, "Interrupted", type, VSL_tags[tag]);
		h_order_finish(fd);
	}

	last[fd] = tag;

	switch (tag) {
	case SLT_VCL_call:
		if (flg[fd] & F_INVCL)
			vsb_cat(ob[fd], "\n");
		else
			flg[fd] |= F_INVCL;
		vsb_printf(ob[fd], "%5d %-12s %c %.*s",
		    fd, VSL_tags[tag], type, len, ptr);
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
	    fd, VSL_tags[tag], type, len, ptr);
	switch (tag) {
	case SLT_ReqEnd:
	case SLT_BackendClose:
	case SLT_BackendReuse:
	case SLT_StatSess:
		h_order_finish(fd);
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
	const char *error;
	int erroroffset;

	if (argc == 2) {
		match_tag = name2tag(argv[0]);
		if (match_tag < 0) {
			fprintf(stderr, "Tag \"%s\" unknown\n", argv[0]);
			exit(2);
		}
		match_re = VRE_compile(argv[1], 0, &error, &erroroffset);
		if (match_re == NULL) {
			fprintf(stderr, "Invalid regex: %s\n", error);
			exit(2);
		}
	}
	if (!b_flag) {
		VSL_Select(vd, SLT_SessionOpen);
		VSL_Select(vd, SLT_SessionClose);
		VSL_Select(vd, SLT_ReqEnd);
	}
	if (!c_flag) {
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

static volatile sig_atomic_t reopen;

static void
sighup(int sig)
{

	(void)sig;
	reopen = 1;
}

static int
open_log(const char *w_arg, int a_flag)
{
	int fd, flags;

	flags = (a_flag ? O_APPEND : O_TRUNC) | O_WRONLY | O_CREAT;
#ifdef O_LARGEFILE
	flags |= O_LARGEFILE;
#endif
	if (!strcmp(w_arg, "-"))
		fd = STDOUT_FILENO;
	else
		fd = open(w_arg, flags, 0644);
	if (fd < 0) {
		perror(w_arg);
		exit(1);
	}
	return (fd);
}

static void
do_write(struct VSL_data *vd, const char *w_arg, int a_flag)
{
	int fd, i, l;
	unsigned char *p;

	fd = open_log(w_arg, a_flag);
	signal(SIGHUP, sighup);
	while (1) {
		i = VSL_NextLog(vd, &p);
		if (i < 0)
			break;
		if (i > 0) {
			l = SHMLOG_LEN(p);
			i = write(fd, p, SHMLOG_NEXTTAG + l);
			if (i < 0) {
				perror(w_arg);
				exit(1);
			}
		}
		if (reopen) {
			close(fd);
			fd = open_log(w_arg, a_flag);
			reopen = 0;
		}
	}
	exit(0);
}

/*--------------------------------------------------------------------*/

static void
usage(void)
{
	fprintf(stderr, "usage: varnishlog "
	    "%s [-aDoV] [-n varnish_name] [-P file] [-w file]\n", VSL_USAGE);
	exit(1);
}

int
main(int argc, char **argv)
{
	int c;
	int a_flag = 0, D_flag = 0, o_flag = 0, u_flag = 0;
	const char *P_arg = NULL;
	const char *w_arg = NULL;
	struct pidfh *pfh = NULL;
	struct VSL_data *vd;

	vd = VSL_New();

	while ((c = getopt(argc, argv, VSL_ARGS "aDoP:uVw:")) != -1) {
		switch (c) {
		case 'a':
			a_flag = 1;
			break;
		case 'b':
			b_flag = 1;
			VSL_Arg(vd, c, optarg);
			break;
		case 'c':
			c_flag = 1;
			VSL_Arg(vd, c, optarg);
			break;
		case 'D':
			D_flag = 1;
			break;
		case 'o':
			o_flag = 1;
			break;
		case 'P':
			P_arg = optarg;
			break;
		case 'u':
			u_flag = 1;
			break;
		case 'V':
			varnish_version("varnishlog");
			exit(0);
		case 'w':
			w_arg = optarg;
			break;
		default:
			if (VSL_Arg(vd, c, optarg) > 0)
				break;
			usage();
		}
	}

	if (o_flag && w_arg != NULL)
		usage();

	if (VSL_OpenLog(vd))
		exit(1);

	if (P_arg && (pfh = vpf_open(P_arg, 0644, NULL)) == NULL) {
		perror(P_arg);
		exit(1);
	}

	if (D_flag && varnish_daemon(0, 0) == -1) {
		perror("daemon()");
		if (pfh != NULL)
			vpf_remove(pfh);
		exit(1);
	}

	if (pfh != NULL)
		vpf_write(pfh);

	if (w_arg != NULL)
		do_write(vd, w_arg, a_flag);

	if (o_flag)
		do_order(vd, argc - optind, argv + optind);

	if (u_flag)
		setbuf(stdout, NULL);

	while (VSL_Dispatch(vd, VSL_H_Print, stdout) >= 0) {
		if (fflush(stdout) != 0) {
			perror("stdout");
			break;
		}
	}

	if (pfh != NULL)
		vpf_remove(pfh);
	exit(0);
}
