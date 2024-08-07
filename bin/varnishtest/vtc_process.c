/*-
 * Copyright (c) 2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dridi Boukelmoune <dridi@varnish-software.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 * XXX:
 *	-ignore-stderr (otherwise output to stderr is fail)
 */

#include "config.h"

#include <sys/ioctl.h>		// Linux: struct winsize

#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __sun
#  include <stropts.h>
#endif
#include <termios.h>
#include <unistd.h>

#include "vtc.h"

#include "vre.h"
#include "vev.h"
#include "vlu.h"
#include "vsb.h"
#include "vsub.h"

#include "teken.h"

struct process {
	unsigned		magic;
#define PROCESS_MAGIC		0x1617b43e
	char			*name;
	struct vtclog		*vl;
	VTAILQ_ENTRY(process)	list;

	char			*spec;
	char			*dir;
	char			*out;
	char			*err;
	int			fd_term;
	int			fd_stderr;
	int			f_stdout;
	int			f_stderr;
	struct vlu		*vlu_stdout;
	struct vlu		*vlu_stderr;
	int			log;
	pid_t			pid;
	int			expect_exit;
	int			expect_signal;
	int			allow_core;

	uintmax_t		stdout_bytes;
	uintmax_t		stderr_bytes;

	pthread_mutex_t		mtx;
	pthread_t		tp;
	unsigned		hasthread;

	int			nlin;
	int			ncol;
	int			ansi_response;
	char			**vram;
	teken_t			tek[1];
};

static VTAILQ_HEAD(, process)	processes =
    VTAILQ_HEAD_INITIALIZER(processes);

static void term_resize(struct process *pp, int lin, int col);

/**********************************************************************
 * Terminal emulation
 */

static void
term_cursor(void *priv, const teken_pos_t *pos)
{
	(void)priv;
	(void)pos;
}

static void
term_putchar(void *priv, const teken_pos_t *pos, teken_char_t ch,
    const teken_attr_t *at)
{
	struct process *pp;

	CAST_OBJ_NOTNULL(pp, priv, PROCESS_MAGIC);
	(void)at;
	if (ch > 126 || ch < 32)
		ch = '?';
	assert(pos->tp_row < pp->nlin);
	assert(pos->tp_col < pp->ncol);
	pp->vram[pos->tp_row][pos->tp_col] = ch;
}

static void
term_fill(void *priv, const teken_rect_t *r, teken_char_t c,
    const teken_attr_t *a)
{
	teken_pos_t p;

	/* Braindead implementation of fill() - just call putchar(). */
	for (p.tp_row = r->tr_begin.tp_row;
	    p.tp_row < r->tr_end.tp_row; p.tp_row++)
		for (p.tp_col = r->tr_begin.tp_col;
		    p.tp_col < r->tr_end.tp_col; p.tp_col++)
			term_putchar(priv, &p, c, a);
}

static void
term_copy(void *priv, const teken_rect_t *r, const teken_pos_t *p)
{
	struct process *pp;
	int nrow, ncol, y; /* Has to be signed - >= 0 comparison */

	/*
	 * Copying is a little tricky. We must make sure we do it in
	 * correct order, to make sure we don't overwrite our own data.
	 */
	CAST_OBJ_NOTNULL(pp, priv, PROCESS_MAGIC);

	nrow = r->tr_end.tp_row - r->tr_begin.tp_row;
	ncol = r->tr_end.tp_col - r->tr_begin.tp_col;

	if (p->tp_row < r->tr_begin.tp_row) {
		/* Copy from top to bottom. */
		for (y = 0; y < nrow; y++)
			memmove(&pp->vram[p->tp_row + y][p->tp_col],
			    &pp->vram[r->tr_begin.tp_row + y][r->tr_begin.tp_col], ncol);
	} else {
		/* Copy from bottom to top. */
		for (y = nrow - 1; y >= 0; y--)
			memmove(&pp->vram[p->tp_row + y][p->tp_col],
			    &pp->vram[r->tr_begin.tp_row + y][r->tr_begin.tp_col], ncol);
	}
}

static void
term_respond(void *priv, const void *p, size_t l)
{
	struct process *pp;
	int r;

	CAST_OBJ_NOTNULL(pp, priv, PROCESS_MAGIC);

	vtc_dump(pp->vl, 4, "term_response", p, l);
	if (pp->ansi_response) {
		r = write(pp->fd_term, p, l);
		if (r != l)
			vtc_fatal(pp->vl, "Could not write to process: %s",
			    strerror(errno));
	}
}

static void
term_param(void *priv, int p, unsigned int v)
{
	struct process *pp;

	CAST_OBJ_NOTNULL(pp, priv, PROCESS_MAGIC);
	if (p == TP_132COLS && v)
		term_resize(pp, pp->nlin, 132);
	if (p == TP_132COLS && !v)
		term_resize(pp, pp->nlin, 80);
}

static const teken_funcs_t process_teken_func = {
	.tf_cursor	=	term_cursor,
	.tf_putchar	=	term_putchar,
	.tf_fill	=	term_fill,
	.tf_copy	=	term_copy,
	.tf_respond	=	term_respond,
	.tf_param	=	term_param,
};

static void
term_screen_dump(const struct process *pp)
{
	int i;
	const teken_pos_t *pos;

	for (i = 0; i < pp->nlin; i++)
		vtc_dump(pp->vl, 3, "screen", pp->vram[i], pp->ncol);
	pos = teken_get_cursor(pp->tek);
	vtc_log(pp->vl, 3, "Cursor at line %d column %d",
	    pos->tp_row + 1, pos->tp_col + 1);
}

static void
term_resize(struct process *pp, int lin, int col)
{
	teken_pos_t pos;
	char **vram;
	int i, j;

	vram = calloc(lin, sizeof *pp->vram);
	AN(vram);
	for (i = 0; i < lin; i++) {
		vram[i] = calloc(col + 1L, 1);
		AN(vram[i]);
		memset(vram[i], ' ', col);
		vram[i][col] = '\0';
	}
	if (pp->vram != NULL) {
		for (i = 0; i < lin; i++) {
			if (i >= pp->nlin)
				break;
			j = col;
			if (j > pp->ncol)
				j = pp->ncol;
			memcpy(vram[i], pp->vram[i], j);
		}
		for (i = 0; i < pp->nlin; i++)
			free(pp->vram[i]);
		free(pp->vram);
	}
	pp->vram = vram;
	pp->nlin = lin;
	pp->ncol = col;

	pos.tp_row = lin;
	pos.tp_col = col;
	teken_set_winsize(pp->tek, &pos);
}

static int
term_find_textline(const struct process *pp, int *x, int y, const char *pat)
{
	const char *t;
	int l;

	if (*x == 0) {
		t = strstr(pp->vram[y], pat);
		if (t != NULL) {
			*x = 1 + (t - pp->vram[y]);
			return (1);
		}
	} else if (*x <= pp->ncol) {
		t = pp->vram[y] + *x - 1;
		l = strlen(pat);
		assert((*x - 1) + (l - 1) < pp->ncol);
		if (!memcmp(t, pat, l))
			return (1);
	}
	return (0);
}

static int
term_find_text(const struct process *pp, int *x, int *y, const char *pat)
{
	int yy;

	if (*y == 0) {
		for (yy = 0; yy < pp->nlin; yy++) {
			if (term_find_textline(pp, x, yy, pat)) {
				*y = yy + 1;
				return (1);
			}
		}
	} else if (*y <= pp->nlin) {
		if (term_find_textline(pp, x, *y - 1, pat))
			return (1);
	}
	return (0);
}

static void
term_expect_text(struct process *pp,
    const char *lin, const char *col, const char *pat)
{
	int x, y, l, d = 10000;
	char *t;

	y = strtoul(lin, NULL, 0);
	if (y < 0 || y > pp->nlin)
		vtc_fatal(pp->vl, "YYY %d nlin %d", y, pp->nlin);
	x = strtoul(col, NULL, 0);
	for(l = 0; l <= 10 && x > pp->ncol; l++)	// wait for screen change
		usleep(100000);
	if (x < 0 || x > pp->ncol)
		vtc_fatal(pp->vl, "XXX %d ncol %d", x, pp->ncol);
	l = strlen(pat);
	if (x + l - 1 > pp->ncol)
		vtc_fatal(pp->vl, "XXX %d ncol %d", x + l - 1, pp->ncol);
	PTOK(pthread_mutex_lock(&pp->mtx));
	while (!term_find_text(pp, &x, &y, pat)) {
		if (x != 0 && y != 0) {
			t = pp->vram[y - 1] + x - 1;
			vtc_log(pp->vl, 4,
			    "text at %d,%d: '%.*s'", y, x, l, t);
		}
		PTOK(pthread_mutex_unlock(&pp->mtx));
		usleep(d);
		PTOK(pthread_mutex_lock(&pp->mtx));
		if (d < 3000000)
			d += d;
	}
	PTOK(pthread_mutex_unlock(&pp->mtx));
	vtc_log(pp->vl, 4, "found expected text at %d,%d: '%s'", y, x, pat);
}

static void
term_expect_cursor(const struct process *pp, const char *lin, const char *col)
{
	int x, y, l;
	const teken_pos_t *pos;

	pos = teken_get_cursor(pp->tek);
	y = strtoul(lin, NULL, 0);
	if (y < 0 || y > pp->nlin)
		vtc_fatal(pp->vl, "YYY %d nlin %d", y, pp->nlin);
	x = strtoul(col, NULL, 0);
	for(l = 0; l < 10 && x > pp->ncol; l++)	// wait for screen change
		usleep(100000);
	if (x < 0 || x > pp->ncol)
		vtc_fatal(pp->vl, "XXX %d ncol %d", x, pp->ncol);
	if (y != 0 && (y-1) != pos->tp_row)
		vtc_fatal(pp->vl, "Cursor on line %d (expected %d)",
		    pos->tp_row + 1, y);
	if (x != 0 && (x-1) != pos->tp_col)
		vtc_fatal(pp->vl, "Cursor in column %d (expected %d)",
		    pos->tp_col + 1, y);
}

static void
term_match_text(struct process *pp,
    const char *lin, const char *col, const char *re)
{
	int i, l, err, erroff;
	struct vsb *vsb, re_vsb[1];
	size_t len;
	ssize_t x, y;
	vre_t *vre;
	char errbuf[VRE_ERROR_LEN];

	vsb = VSB_new_auto();
	AN(vsb);

	y = strtoul(lin, NULL, 0);
	if (y < 0 || y > pp->nlin)
		vtc_fatal(pp->vl, "YYY %zd nlin %d", y, pp->nlin);
	x = strtoul(col, NULL, 0);
	for(l = 0; l < 10 && x > pp->ncol; l++)	// wait for screen change
		usleep(100000);
	if (x < 0 || x > pp->ncol)
		vtc_fatal(pp->vl, "XXX %zd ncol %d", x, pp->ncol);

	if (x)
		x--;

	if (y)
		y--;

	vre = VRE_compile(re, 0, &err, &erroff, 1);
	if (vre == NULL) {
		AN(VSB_init(re_vsb, errbuf, sizeof errbuf));
		AZ(VRE_error(re_vsb, err));
		AZ(VSB_finish(re_vsb));
		VSB_fini(re_vsb);
		vtc_fatal(pp->vl, "invalid regexp \"%s\" at %d (%s)",
		    re, erroff, errbuf);
	}

	PTOK(pthread_mutex_lock(&pp->mtx));

	len = (pp->nlin - y) * (pp->ncol - x);
	for (i = y; i < pp->nlin; i++) {
		VSB_bcat(vsb, &pp->vram[i][x], pp->ncol - x);
		VSB_putc(vsb, '\n');
	}

	AZ(VSB_finish(vsb));

	if (VRE_match(vre, VSB_data(vsb), len, 0, NULL) < 1)
		vtc_fatal(pp->vl, "match failed: (\"%s\")", re);
	else
		vtc_log(pp->vl, 4, "match succeeded");

	PTOK(pthread_mutex_unlock(&pp->mtx));
	VSB_destroy(&vsb);
	VRE_free(&vre);
}

/**********************************************************************
 * Allocate and initialize a process
 */

#define PROCESS_EXPAND(field, format, ...)				\
	do {								\
		vsb = macro_expandf(p->vl, format, __VA_ARGS__);	\
		AN(vsb);						\
		p->field = strdup(VSB_data(vsb));			\
		AN(p->field);						\
		VSB_destroy(&vsb);					\
	} while (0)

static void
process_coverage(struct process *p)
{
	const teken_attr_t *a;
	teken_pos_t pos;
	int fg, bg;

	// Code-Coverage of Teken

	(void)teken_get_sequence(p->tek, TKEY_UP);
	(void)teken_get_sequence(p->tek, TKEY_F1);
	(void)teken_256to8(0);
	(void)teken_256to16(0);
	a = teken_get_defattr(p->tek);
	teken_set_defattr(p->tek, a);
	a = teken_get_curattr(p->tek);
	teken_set_curattr(p->tek, a);
	(void)teken_get_winsize(p->tek);
	pos.tp_row = 0;
	pos.tp_col = 8;
	teken_set_cursor(p->tek, &pos);
	teken_get_defattr_cons25(p->tek, &fg, &bg);
}

static struct process *
process_new(const char *name)
{
	struct process *p;
	struct vsb *vsb;
	char buf[1024];

	ALLOC_OBJ(p, PROCESS_MAGIC);
	AN(p);
	REPLACE(p->name, name);
	PTOK(pthread_mutex_init(&p->mtx, NULL));

	p->vl = vtc_logopen("%s", name);
	AN(p->vl);

	PROCESS_EXPAND(dir, "${tmpdir}/%s", name);
	PROCESS_EXPAND(out, "${tmpdir}/%s/term", name);
	PROCESS_EXPAND(err, "${tmpdir}/%s/stderr", name);

	bprintf(buf, "rm -rf %s ; mkdir -p %s ; touch %s %s",
	    p->dir, p->dir, p->out, p->err);
	AZ(system(buf));

	p->fd_term = -1;

	VTAILQ_INSERT_TAIL(&processes, p, list);
	teken_init(p->tek, &process_teken_func, p);
	term_resize(p, 24, 80);
	process_coverage(p);
	return (p);
}

#undef PROCESS_EXPAND

/**********************************************************************
 * Clean up process
 */

static void
process_delete(struct process *p)
{
	int i;

	CHECK_OBJ_NOTNULL(p, PROCESS_MAGIC);
	PTOK(pthread_mutex_destroy(&p->mtx));
	vtc_logclose(p->vl);
	free(p->name);
	free(p->dir);
	free(p->out);
	free(p->err);

	for (i = 0; i < p->nlin; i++)
		free(p->vram[i]);
	free(p->vram);

	/*
	 * We do not delete the directory, it may contain useful stdout
	 * and stderr files. They will be deleted on account of belonging
	 * to the test's tmpdir.
	 */

	/* XXX: MEMLEAK (?) */
	FREE_OBJ(p);
}

static void
process_undef(const struct process *p)
{
	CHECK_OBJ_NOTNULL(p, PROCESS_MAGIC);

	macro_undef(p->vl, p->name, "dir");
	macro_undef(p->vl, p->name, "out");
	macro_undef(p->vl, p->name, "err");
}

/**********************************************************************
 * Data stream handling
 */

static int
process_vlu_func(void *priv, const char *l)
{
	struct process *p;

	CAST_OBJ_NOTNULL(p, priv, PROCESS_MAGIC);
	vtc_dump(p->vl, 4, "output", l, -1);
	return (0);
}

static int v_matchproto_(vev_cb_f)
process_stdout(const struct vev *ev, int what)
{
	struct process *p;
	char buf[BUFSIZ];
	int i;

	CAST_OBJ_NOTNULL(p, ev->priv, PROCESS_MAGIC);
	(void)what;
	i = read(p->fd_term, buf, sizeof buf);
	if (i <= 0) {
		vtc_log(p->vl, 4, "stdout read %d", i);
		return (1);
	}
	PTOK(pthread_mutex_lock(&p->mtx));
	p->stdout_bytes += i;
	PTOK(pthread_mutex_unlock(&p->mtx));
	if (p->log == 1)
		(void)VLU_Feed(p->vlu_stdout, buf, i);
	else if (p->log == 2)
		vtc_dump(p->vl, 4, "stdout", buf, i);
	else if (p->log == 3)
		vtc_hexdump(p->vl, 4, "stdout", buf, i);
	assert(write(p->f_stdout, buf, i) == i);
	PTOK(pthread_mutex_lock(&p->mtx));
	teken_input(p->tek, buf, i);
	PTOK(pthread_mutex_unlock(&p->mtx));
	return (0);
}

static int v_matchproto_(vev_cb_f)
process_stderr(const struct vev *ev, int what)
{
	struct process *p;
	char buf[BUFSIZ];
	int i;

	CAST_OBJ_NOTNULL(p, ev->priv, PROCESS_MAGIC);
	(void)what;
	i = read(p->fd_stderr, buf, sizeof buf);
	if (i <= 0) {
		vtc_log(p->vl, 4, "stderr read %d", i);
		return (1);
	}
	PTOK(pthread_mutex_lock(&p->mtx));
	p->stderr_bytes += i;
	PTOK(pthread_mutex_unlock(&p->mtx));
	vtc_dump(p->vl, 4, "stderr", buf, i);
	assert(write(p->f_stderr, buf, i) == i);
	return (0);
}

static void
process_cleanup(void *priv)
{
	struct vev_root *evb = priv;
	VEV_Destroy(&evb);
}

static void *
process_thread(void *priv)
{
	struct process *p;
	struct vev_root *evb;
	struct vev *ev;
	int r;

	CAST_OBJ_NOTNULL(p, priv, PROCESS_MAGIC);

	p->f_stdout = open(p->out, O_WRONLY|O_APPEND);
	assert(p->f_stdout >= 0);
	p->f_stderr = open(p->err, O_WRONLY|O_APPEND);
	assert(p->f_stderr >= 0);

	evb = VEV_New();
	AN(evb);
	pthread_cleanup_push(process_cleanup, evb);

	ev = VEV_Alloc();
	AN(ev);
	ev->fd = p->fd_term;
	ev->fd_flags = VEV__RD | VEV__HUP | VEV__ERR;
	ev->callback = process_stdout;
	ev->priv = p;
	AZ(VEV_Start(evb, ev));

	ev = VEV_Alloc();
	AN(ev);
	ev->fd = p->fd_stderr;
	ev->fd_flags = VEV__RD | VEV__HUP | VEV__ERR;
	ev->callback = process_stderr;
	ev->priv = p;
	AZ(VEV_Start(evb, ev));

	if (p->log == 1) {
		p->vlu_stdout = VLU_New(process_vlu_func, p, 1024);
		AN(p->vlu_stdout);
		p->vlu_stderr = VLU_New(process_vlu_func, p, 1024);
		AN(p->vlu_stderr);
	}

	do {
		r = VEV_Once(evb);
	} while (r == 1);

	if (r < 0)
		vtc_fatal(p->vl, "VEV_Once() = %d, error %s", r,
		    strerror(errno));

	vtc_wait4(p->vl, p->pid,
	    p->expect_exit, p->expect_signal, p->allow_core);
	closefd(&p->f_stdout);
	closefd(&p->f_stderr);

	PTOK(pthread_mutex_lock(&p->mtx));

	/* NB: We keep the other macros around */
	macro_undef(p->vl, p->name, "pid");
	p->pid = -1;

	PTOK(pthread_mutex_unlock(&p->mtx));

	pthread_cleanup_pop(0);
	VEV_Destroy(&evb);
	if (p->log == 1) {
		VLU_Destroy(&p->vlu_stdout);
		VLU_Destroy(&p->vlu_stderr);
	}
	return (NULL);
}

static void
process_winsz(struct process *p, int fd)
{
	struct winsize ws;
	int i;

	memset(&ws, 0, sizeof ws);
	ws.ws_row = (short)p->nlin;
	ws.ws_col = (short)p->ncol;
	i = ioctl(fd, TIOCSWINSZ, &ws);
	if (i)
		vtc_log(p->vl, 4, "TIOCWINSZ %d %s", i, strerror(errno));
}

static void
process_init_term(struct process *p, int fd)
{
	struct termios tt;
	int i;

	process_winsz(p, fd);

	memset(&tt, 0, sizeof tt);
	tt.c_cflag = CREAD | CS8 | HUPCL;
	tt.c_iflag = BRKINT | ICRNL | IMAXBEL | IXON | IXANY;
	tt.c_lflag = ICANON | ISIG | IEXTEN | ECHO | ECHOE | ECHOKE | ECHOCTL;
	tt.c_oflag = OPOST | ONLCR;
	i = cfsetispeed(&tt, B9600);
	if (i)
		vtc_log(p->vl, 4, "cfsetispeed %d %s", i, strerror(errno));
	i = cfsetospeed(&tt, B9600);
	if (i)
		vtc_log(p->vl, 4, "cfsetospeed %d %s", i, strerror(errno));
	tt.c_cc[VEOF] = '\x04';			// CTRL-D
	tt.c_cc[VERASE] = '\x08';		// CTRL-H (Backspace)
	tt.c_cc[VKILL] = '\x15';		// CTRL-U
	tt.c_cc[VINTR] = '\x03';		// CTRL-C
	tt.c_cc[VQUIT] = '\x1c';		// CTRL-backslash

	i = tcsetattr(fd, TCSAFLUSH, &tt);
	if (i)
		vtc_log(p->vl, 4, "TCSAFLUSH %d %s", i, strerror(errno));
}

/**********************************************************************
 * Start the process thread
 */

static void
process_start(struct process *p)
{
	struct vsb *cl;
	int fd2[2];
	int master, slave;
	const char *slavename;
	char c;

	CHECK_OBJ_NOTNULL(p, PROCESS_MAGIC);
	if (p->hasthread)
		vtc_fatal(p->vl, "Already running, -wait first");

	vtc_log(p->vl, 4, "CMD: %s", p->spec);

	cl = macro_expand(p->vl, p->spec);
	AN(cl);

	master = posix_openpt(O_RDWR|O_NOCTTY);
	assert(master >= 0);
	AZ(grantpt(master));
	AZ(unlockpt(master));
	slavename = ptsname(master);
	AN(slavename);

	AZ(pipe(fd2));

	p->pid = fork();
	assert(p->pid >= 0);
	if (p->pid == 0) {
		assert(setsid() == getpid());
		assert(dup2(fd2[1], STDERR_FILENO) == STDERR_FILENO);
		AZ(close(STDIN_FILENO));
		slave = open(slavename, O_RDWR);
		assert(slave == STDIN_FILENO);
#ifdef __sun
		if (ioctl(slave, I_PUSH, "ptem"))
			vtc_log(p->vl, 4, "PUSH ptem: %s", strerror(errno));
		if (ioctl(slave, I_PUSH, "ldterm"))
			vtc_log(p->vl, 4, "PUSH ldterm: %s", strerror(errno));
		(void)ioctl(STDIN_FILENO, TIOCSCTTY, NULL);
#else
		AZ(ioctl(STDIN_FILENO, TIOCSCTTY, NULL));
#endif
		AZ(close(STDOUT_FILENO));
		assert(dup2(slave, STDOUT_FILENO) == STDOUT_FILENO);
		VSUB_closefrom(STDERR_FILENO + 1);
		process_init_term(p, slave);

		AZ(setenv("TERM", "xterm", 1));
		AZ(unsetenv("TERMCAP"));
		// Not using NULL because GCC is now even more demented...
		assert(write(STDERR_FILENO, "+", 1) == 1);
		AZ(execl("/bin/sh", "/bin/sh", "-c", VSB_data(cl), (char*)0));
		exit(1);
	}
	vtc_log(p->vl, 3, "PID: %ld", (long)p->pid);
	VSB_destroy(&cl);

	assert(read(fd2[0], &c, 1) == 1);
	p->fd_term = master;
	closefd(&fd2[1]);
	p->fd_stderr = fd2[0];
	macro_def(p->vl, p->name, "pid", "%ld", (long)p->pid);
	macro_def(p->vl, p->name, "dir", "%s", p->dir);
	macro_def(p->vl, p->name, "out", "%s", p->out);
	macro_def(p->vl, p->name, "err", "%s", p->err);
	p->hasthread = 1;
	PTOK(pthread_create(&p->tp, NULL, process_thread, p));
}

/**********************************************************************
 * Wait for process thread to stop
 */

static void
process_wait(struct process *p)
{
	void *v;

	if (p->hasthread) {
		PTOK(pthread_join(p->tp, &v));
		p->hasthread = 0;
	}
	vtc_log(p->vl, 4, "stdout %ju bytes, stderr %ju bytes",
	    p->stdout_bytes, p->stderr_bytes);
}

/**********************************************************************
 * Send a signal to a process
 */

static void
process_kill(struct process *p, const char *sig)
{
	int j = 0;
	pid_t pid;

	CHECK_OBJ_NOTNULL(p, PROCESS_MAGIC);
	AN(sig);

	PTOK(pthread_mutex_lock(&p->mtx));
	pid = p->pid;
	PTOK(pthread_mutex_unlock(&p->mtx));

	if (pid <= 0)
		vtc_fatal(p->vl, "Cannot signal a non-running process");

	if (!strcmp(sig, "TERM"))
		j = SIGTERM;
	else if (!strcmp(sig, "INT"))
		j = SIGINT;
	else if (!strcmp(sig, "KILL"))
		j = SIGKILL;
	else if (!strcmp(sig, "HUP"))
		j = SIGHUP;
	else if (*sig == '-')
		j = strtoul(sig + 1, NULL, 10);
	else
		vtc_fatal(p->vl, "Could not grok signal (%s)", sig);

	if (p->expect_signal == 0)
		p->expect_signal = -j;
	if (kill(-pid, j) < 0)
		vtc_fatal(p->vl, "Failed to send signal %d (%s)",
		    j, strerror(errno));
	else
		vtc_log(p->vl, 4, "Sent signal %d", j);
}

/**********************************************************************
 * Write to a process' stdin
 */

static void
process_write(const struct process *p, const char *text)
{
	int r, len;

	if (!p->hasthread)
		vtc_fatal(p->vl, "Cannot write to a non-running process");

	len = strlen(text);
	vtc_log(p->vl, 4, "Writing %d bytes", len);
	r = write(p->fd_term, text, len);
	if (r != len)
		vtc_fatal(p->vl, "Failed to write: len=%d %s (%d)",
		    len, strerror(errno), errno);
}

static void
process_write_hex(const struct process *p, const char *text)
{
	struct vsb *vsb;

	if (!p->hasthread)
		vtc_fatal(p->vl, "Cannot write to a non-running process");

	vsb = vtc_hex_to_bin(p->vl, text);
	assert(VSB_len(vsb) >= 0);
	vtc_hexdump(p->vl, 4, "sendhex", VSB_data(vsb), VSB_len(vsb));
	AZ(VSB_tofile(vsb, p->fd_term));
	VSB_destroy(&vsb);
}

static void
process_close(struct process *p)
{

	if (!p->hasthread)
		vtc_fatal(p->vl, "Cannot close a non-running process");

	process_kill(p, "HUP");
}

/* SECTION: process process
 *
 * Run a process with stdin+stdout on a pseudo-terminal and stderr on a pipe.
 *
 * Output from the pseudo-terminal is copied verbatim to ${pNAME_out},
 * and the -log/-dump/-hexdump flags will also put it in the vtc-log.
 *
 * The pseudo-terminal is not in ECHO mode, but if the programs run set
 * it to ECHO mode ("stty sane") any input sent to the process will also
 * appear in this stream because of the ECHO.
 *
 * Output from the stderr-pipe is copied verbatim to ${pNAME_err}, and
 * is always included in the vtc_log.
 *
 *	process pNAME SPEC [-allow-core] [-expect-exit N] [-expect-signal N]
 *		[-dump] [-hexdump] [-log]
 *		[-run] [-close] [-kill SIGNAL] [-start] [-stop] [-wait]
 *		[-write STRING] [-writeln STRING] [-writehex HEXSTRING]
 *		[-need-bytes [+]NUMBER]
 *		[-screen-dump] [-winsz LINES COLUMNS] [-ansi-response]
 *		[-expect-cursor LINE COLUMN] [-expect-text LINE COLUMN TEXT]
 *		[-match-text LINE COLUMN REGEXP]
 *
 * pNAME
 *	Name of the process. It must start with 'p'.
 *
 * SPEC
 *	The command(s) to run in this process.
 *
 * \-hexdump
 *	Log output with vtc_hexdump(). Must be before -start/-run.
 *
 * \-dump
 *	Log output with vtc_dump(). Must be before -start/-run.
 *
 * \-log
 *	Log output with VLU/vtc_log(). Must be before -start/-run.
 *
 * \-start
 *	Start the process.
 *
 * \-expect-exit N
 *	Expect exit status N
 *
 * \-expect-signal N
 *	Expect signal in exit status N
 *
 * \-allow-core
 *	Core dump in exit status is OK
 *
 * \-wait
 *	Wait for the process to finish.
 *
 * \-run
 *	Shorthand for -start -wait.
 *
 *	In most cases, if you just want to start a process and wait for it
 *	to finish, you can use the ``shell`` command instead.
 *	The following commands are equivalent::
 *
 *	    shell "do --something"
 *
 *	    process p1 "do --something" -run
 *
 *	However, you may use the the ``process`` variant to conveniently
 *	collect the standard input and output without dealing with shell
 *	redirections yourself. The ``shell`` command can also expect an
 *	expression from either output, consider using it if you only need
 *	to match one.
 *
 * \-key KEYSYM
 *      Send emulated key-press.
 *      KEYSYM can be one of (NPAGE, PPAGE, HOME, END)
 *
 *
 * \-kill SIGNAL
 *	Send a signal to the process. The argument can be either
 *	the string "TERM", "INT", or "KILL" for SIGTERM, SIGINT or SIGKILL
 *	signals, respectively, or a hyphen (-) followed by the signal
 *	number.
 *
 *	If you need to use other signal names, you can use the ``kill``\(1)
 *	command directly::
 *
 *	    shell "kill -USR1 ${pNAME_pid}"
 *
 *	Note that SIGHUP usage is discouraged in test cases.
 *
 * \-stop
 *	Shorthand for -kill TERM.
 *
 * \-close
 *	Alias for "-kill HUP"
 *
 * \-winsz LINES COLUMNS
 *	Change the terminal window size to LIN lines and COL columns.
 *
 * \-write STRING
 *	Write a string to the process' stdin.
 *
 * \-writeln STRING
 *	Same as -write followed by a newline (\\n).
 *
 * \-writehex HEXSTRING
 *	Same as -write but interpreted as hexadecimal bytes.
 *
 * \-need-bytes [+]NUMBER
 *	Wait until at least NUMBER bytes have been received in total.
 *	If '+' is prefixed, NUMBER new bytes must be received.
 *
 * \-ansi-response
 *	Respond to terminal respond-back sequences
 *
 * \-expect-cursor LINE COLUMN
 *	Expect cursors location
 *
 * \-expect-text LINE COLUMNS TEXT
 *	Wait for TEXT to appear at LIN,COL on the virtual screen.
 *	Lines and columns are numbered 1...N
 *	LIN==0 means "on any line"
 *	COL==0 means "anywhere on the line"
 *
 * \-match-text LINE COLUMN REGEXP
 *	Wait for the PAT regular expression to match the text at LIN,COL on the virtual screen.
 *	Lines and columns are numbered 1...N
 *	LIN==0 means "on any line"
 *	COL==0 means "anywhere on the line"
 *
 *
 * \-screen-dump
 *	Dump the virtual screen into vtc_log
 *
 */

void
cmd_process(CMD_ARGS)
{
	struct process *p, *p2;
	uintmax_t u, v, bsnap;
	unsigned lin,col;
	int spec_set = 0;

	(void)priv;

	if (av == NULL) {
		/* Reset and free */
		VTAILQ_FOREACH_SAFE(p, &processes, list, p2) {
			if (p->pid > 0) {
				process_kill(p, "TERM");
				sleep(1);
				if (p->pid > 0)
					process_kill(p, "KILL");
			}
			if (p->hasthread)
				process_wait(p);
			VTAILQ_REMOVE(&processes, p, list);
			process_undef(p);
			process_delete(p);
		}
		return;
	}

	AZ(strcmp(av[0], "process"));
	av++;

	VTC_CHECK_NAME(vl, av[0], "Process", 'p');
	VTAILQ_FOREACH(p, &processes, list)
		if (!strcmp(p->name, av[0]))
			break;
	if (p == NULL)
		p = process_new(av[0]);
	av++;

	PTOK(pthread_mutex_lock(&p->mtx));
	bsnap = p->stdout_bytes;
	PTOK(pthread_mutex_unlock(&p->mtx));

	for (; *av != NULL; av++) {
		if (vtc_error)
			break;

		if (!strcmp(*av, "-allow-core")) {
			p->allow_core = 1;
			continue;
		}
		if (!strcmp(*av, "-close")) {
			process_close(p);
			continue;
		}
		if (!strcmp(*av, "-dump")) {
			if (p->hasthread)
				vtc_fatal(p->vl,
				    "Cannot dump a running process");
			p->log = 2;
			continue;
		}
		if (!strcmp(*av, "-expect-exit")) {
			p->expect_exit = strtoul(av[1], NULL, 0);
			av++;
			continue;
		}
		if (!strcmp(*av, "-expect-signal")) {
			p->expect_signal = strtoul(av[1], NULL, 0);
			av++;
			continue;
		}
		if (!strcmp(*av, "-hexdump")) {
			if (p->hasthread)
				vtc_fatal(p->vl,
				    "Cannot dump a running process");
			p->log = 3;
			continue;
		}
		if (!strcmp(*av, "-key")) {
			if (!strcmp(av[1], "NPAGE"))
				process_write(p, "\x1b\x5b\x36\x7e");
			else if (!strcmp(av[1], "PPAGE"))
				process_write(p, "\x1b\x5b\x35\x7e");
			else if (!strcmp(av[1], "HOME"))
				process_write(p, "\x1b\x4f\x48");
			else if (!strcmp(av[1], "END"))
				process_write(p, "\x1b\x4f\x46");
			else
				vtc_fatal(p->vl, "Unknown key %s", av[1]);
			continue;
		}
		if (!strcmp(*av, "-kill")) {
			process_kill(p, av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-log")) {
			if (p->hasthread)
				vtc_fatal(p->vl,
				    "Cannot log a running process");
			p->log = 1;
			continue;
		}
		if (!strcmp(*av, "-need-bytes")) {
			u = strtoumax(av[1], NULL, 0);
			if (av[1][0] == '+')
				u += bsnap;
			av++;
			do {
				PTOK(pthread_mutex_lock(&p->mtx));
				v = p->stdout_bytes;
				PTOK(pthread_mutex_unlock(&p->mtx));
				vtc_log(p->vl, 4, "Have %ju bytes", v);
				usleep(500000);
			} while(v < u);
			continue;
		}
		if (!strcmp(*av, "-run")) {
			process_start(p);
			process_wait(p);
			continue;
		}
		if (!strcmp(*av, "-ansi-response")) {
			p->ansi_response = 1;
			continue;
		}
		if (!strcmp(*av, "-expect-text")) {
			AN(av[1]);
			AN(av[2]);
			AN(av[3]);
			term_expect_text(p, av[1], av[2], av[3]);
			av += 3;
			continue;
		}
		if (!strcmp(*av, "-expect-cursor")) {
			AN(av[1]);
			AN(av[2]);
			term_expect_cursor(p, av[1], av[2]);
			av += 2;
			continue;
		}
		if (!strcmp(*av, "-match-text")) {
			AN(av[1]);
			AN(av[2]);
			AN(av[3]);
			term_match_text(p, av[1], av[2], av[3]);
			av += 3;
			continue;
		}
		if (!strcmp(*av, "-screen_dump") ||
		    !strcmp(*av, "-screen-dump")) {
			term_screen_dump(p);
			continue;
		}
		if (!strcmp(*av, "-start")) {
			process_start(p);
			continue;
		}
		if (!strcmp(*av, "-stop")) {
			process_kill(p, "TERM");
			sleep(1);
			continue;
		}
		if (!strcmp(*av, "-wait")) {
			process_wait(p);
			continue;
		}
		if (!strcmp(*av, "-winsz")) {
			lin = atoi(av[1]);
			assert(lin > 1);
			col = atoi(av[2]);
			assert(col > 1);
			av += 2;
			PTOK(pthread_mutex_lock(&p->mtx));
			term_resize(p, lin, col);
			PTOK(pthread_mutex_unlock(&p->mtx));
			process_winsz(p, p->fd_term);
			continue;
		}
		if (!strcmp(*av, "-write")) {
			process_write(p, av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-writehex")) {
			process_write_hex(p, av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-writeln")) {
			process_write(p, av[1]);
			process_write(p, "\n");
			av++;
			continue;
		}
		if (**av == '-' || spec_set)
			vtc_fatal(p->vl, "Unknown process argument: %s", *av);
		REPLACE(p->spec, *av);
		spec_set = 1;
	}
}
