/*
 * Copyright (c) 2008-2010 Redpill Linpro AS
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

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "libvarnish.h"
#include "vev.h"
#include "vsb.h"
#include "vqueue.h"
#include "miniobj.h"

#include "vtc.h"

#define		MAX_FILESIZE		(1024 * 1024)


struct vtc_tst {
	unsigned		magic;
#define TST_MAGIC		0x618d8b88
	VTAILQ_ENTRY(vtc_tst)	list;
	const char		*filename;
	char			*script;
	unsigned		ntodo;
};

struct vtc_job {
	unsigned		magic;
#define JOB_MAGIC		0x1b5fc419
	struct vtc_tst		*tst;
	pid_t			child;
	struct vev		*ev;
	struct vev		*evt;
	char			*buf;
	char			*tmpdir;
	unsigned		bufsiz;
	double			t0;
};

static VTAILQ_HEAD(, vtc_tst) tst_head = VTAILQ_HEAD_INITIALIZER(tst_head);
static struct vev_base *vb;
static int njob = 0;
static int npar = 1;			/* Number of parallel tests */
static unsigned vtc_maxdur = 30;	/* Max duration of any test */
static int vtc_continue;		/* Continue on error */
static int vtc_verbosity = 1;		/* Verbosity Level */
static int vtc_good;
static int vtc_fail;

/**********************************************************************
 * Read a file into memory
 */

static char *
read_file(const char *fn)
{
	char *buf;
	ssize_t sz = MAX_FILESIZE;
	ssize_t s;
	int fd;

	fd = open(fn, O_RDONLY);
	if (fd < 0)
		return (NULL);
	buf = malloc(sz);
	assert(buf != NULL);
	s = read(fd, buf, sz - 1);
	if (s <= 0) {
		free(buf);
		return (NULL);
	}
	AZ(close (fd));
	assert(s < sz);		/* XXX: increase MAX_FILESIZE */
	buf[s] = '\0';
	buf = realloc(buf, s + 1);
	assert(buf != NULL);
	return (buf);
}

/**********************************************************************
 * Print usage
 */

static void
usage(void)
{
	fprintf(stderr, "usage: varnishtest [-n iter] [-qv] file ...\n");
	exit(1);
}

/**********************************************************************
 * CallBack
 */

static int
tst_cb(const struct vev *ve, int what)
{
	struct vtc_job *jp;
	char buf[BUFSIZ];
	int i, stx;
	pid_t px;
	double t;

	CAST_OBJ_NOTNULL(jp, ve->priv, JOB_MAGIC);

	// printf("%p %s %d\n", ve, jp->tst->filename, what);
	if (what == 0) {
		/* XXX: Timeout */
		AZ(kill(jp->child, SIGKILL));
		jp->evt = NULL;
		return (1);
	}
	assert(what & (EV_RD | EV_HUP));

	*buf = '\0';
	i = read(ve->fd, buf, sizeof buf - 1);
	if (i > 0) {
		buf[i] = '\0';
		printf("######## %s ########\n%s", jp->tst->filename, buf);
	}
	if (i == 0) {
		njob--;
		px = wait4(jp->child, &stx, 0, NULL);
		t = TIM_mono() - jp->t0;
		AZ(close(ve->fd));

		if (stx && vtc_verbosity)
			printf("%s\n", jp->buf);
		else if (vtc_verbosity > 1)
			printf("%s\n", jp->buf);

		if (stx)
			vtc_fail++;
		else
			vtc_good++;

		if (stx) {
			printf("#     top  TEST %s FAILED (%.3f)\n",
			    jp->tst->filename, t);
			if (!vtc_continue) {
				/* XXX kill -9 other jobs ? */
				exit (2);
			}
		} else if (vtc_verbosity) {
			printf("#     top  TEST %s passed (%.3f)\n",
			    jp->tst->filename, t);
		}
		AZ(munmap(jp->buf, jp->bufsiz));
		if (jp->evt != NULL)
			vev_del(vb, jp->evt);

		bprintf(buf, "rm -rf %s", jp->tmpdir);
		AZ(system(buf));
		free(jp->tmpdir);
		FREE_OBJ(jp);
		return (1);
	}
	return (0);
}

/**********************************************************************
 * Start Test
 */

static void
start_test(void)
{
	struct vtc_tst *tp;
	int p[2], sfd, retval;
	struct vtc_job *jp;
	char tmpdir[PATH_MAX];


	ALLOC_OBJ(jp, JOB_MAGIC);
	AN(jp);

	jp->bufsiz = 64*1024;		/* XXX */

	jp->buf = mmap(NULL, jp->bufsiz, PROT_READ|PROT_WRITE,
	    MAP_ANON | MAP_SHARED, -1, 0);
	assert(jp->buf != MAP_FAILED);
	memset(jp->buf, 0, jp->bufsiz);

	srandomdev();
	bprintf(tmpdir, "/tmp/vtc.%d.%08x", getpid(), (unsigned)random());
	AZ(mkdir(tmpdir, 0700));

	tp = VTAILQ_FIRST(&tst_head);
	CHECK_OBJ_NOTNULL(tp, TST_MAGIC);
	AN(tp->ntodo);
	tp->ntodo--;
	VTAILQ_REMOVE(&tst_head, tp, list);
	if (tp->ntodo >0)
		VTAILQ_INSERT_TAIL(&tst_head, tp, list);

	jp->tst = tp;
	jp->tmpdir = strdup(tmpdir);
	AN(jp->tmpdir);

	AZ(pipe(p));
	assert(p[0] > STDERR_FILENO);
	assert(p[1] > STDERR_FILENO);
	jp->t0 = TIM_mono();
	jp->child = fork();
	assert(jp->child >= 0);
	if (jp->child == 0) {
		AZ(close(STDIN_FILENO));
		assert(open("/dev/null", O_RDONLY) == STDIN_FILENO);
		assert(dup2(p[1], STDOUT_FILENO) == STDOUT_FILENO);
		assert(dup2(p[1], STDERR_FILENO) == STDERR_FILENO);
		for (sfd = STDERR_FILENO + 1; sfd < 100; sfd++)
			(void)close(sfd);
		retval = exec_file(jp->tst->filename, jp->tst->script,
		    jp->tmpdir, jp->buf, jp->bufsiz);
		_exit(retval);
	}
	AZ(close(p[1]));

	jp->ev = vev_new();
	AN(jp->ev);
	jp->ev->fd_flags = EV_RD | EV_HUP | EV_ERR;
	jp->ev->fd = p[0];
	jp->ev->priv = jp;
	jp->ev->callback = tst_cb;
	AZ(vev_add(vb, jp->ev));

	jp->evt = vev_new();
	AN(jp->evt);
	jp->evt->fd = -1;
	jp->evt->timeout = vtc_maxdur;
	jp->evt->priv = jp;
	jp->evt->callback = tst_cb;
	AZ(vev_add(vb, jp->evt));
}

/**********************************************************************
 * Main
 */

int
main(int argc, char * const *argv)
{
	int ch, i;
	int ntest = 1;			/* Run tests this many times */
	struct vtc_tst *tp;
	char *p;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	while ((ch = getopt(argc, argv, "j:kL:n:qt:v")) != -1) {
		switch (ch) {
		case 'j':
			npar = strtoul(optarg, NULL, 0);
			break;
		case 'k':
			vtc_continue = !vtc_continue;
			break;
		case 'n':
			ntest = strtoul(optarg, NULL, 0);
			break;
		case 'q':
			if (vtc_verbosity > 0)
				vtc_verbosity--;
			break;
		case 't':
			vtc_maxdur = strtoul(optarg, NULL, 0);
			break;
		case 'v':
			if (vtc_verbosity < 2)
				vtc_verbosity++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	for (;argc > 0; argc--, argv++) {
		p = read_file(*argv);
		if (p == NULL) {
			fprintf(stderr, "Cannot stat file \"%s\": %s\n",
			    *argv, strerror(errno));
			if (vtc_continue)
				continue;
			exit (2);
		}
		ALLOC_OBJ(tp, TST_MAGIC);
		AN(tp);
		tp->filename = *argv;
		tp->script = p;
		tp->ntodo = ntest;
		VTAILQ_INSERT_TAIL(&tst_head, tp, list);
	}

	vb = vev_new_base();

	i = 0;
	while(!VTAILQ_EMPTY(&tst_head) || i) {
		if (!VTAILQ_EMPTY(&tst_head) && njob < npar) {
			start_test();
			njob++;
			i = 1;
			continue;
		}
		i = vev_schedule_one(vb);
	}
	if (vtc_continue)
		fprintf(stderr, "%d tests failed, %d tests passed\n",
		    vtc_fail, vtc_good);
	if (vtc_fail)
		return (1);
	return (0);
}
