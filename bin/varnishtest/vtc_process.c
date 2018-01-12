/*-
 * Copyright (c) 2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dridi Boukelmoune <dridi@varnish-software.com>
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
#include <sys/resource.h>
#include <sys/wait.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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

#include "vev.h"
#include "vlu.h"
#include "vsb.h"
#include "vsub.h"

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

	pthread_mutex_t		mtx;
	pthread_t		tp;
	unsigned		hasthread;
	int			status;

	unsigned		term_state;
#define NTERMARG		10
	int			term_arg[NTERMARG];
	int			*term_ap;
#define NLINES			24
#define NCOLS			80
	char			*term_c[NLINES];
	int			term_x, term_y;
};

static VTAILQ_HEAD(, process)	processes =
    VTAILQ_HEAD_INITIALIZER(processes);

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

static struct process *
process_new(const char *name)
{
	struct process *p;
	struct vsb *vsb;
	char buf[1024];
	int i;

	ALLOC_OBJ(p, PROCESS_MAGIC);
	AN(p);
	REPLACE(p->name, name);
	AZ(pthread_mutex_init(&p->mtx, NULL));

	p->vl = vtc_logopen(name);
	AN(p->vl);

	PROCESS_EXPAND(dir, "${tmpdir}/%s", name);
	PROCESS_EXPAND(out, "${tmpdir}/%s/term", name);
	PROCESS_EXPAND(err, "${tmpdir}/%s/stderr", name);

	bprintf(buf, "rm -rf %s ; mkdir -p %s ; touch %s %s",
	    p->dir, p->dir, p->out, p->err);
	AZ(system(buf));

	p->fd_term = -1;

	VTAILQ_INSERT_TAIL(&processes, p, list);
	for (i = 0; i < NLINES; i++) {
		p->term_c[i] = malloc(NCOLS + 1);
		AN(p->term_c[i]);
		memset(p->term_c[i], ' ', NCOLS);
		p->term_c[i][NCOLS] = '\0';
	}
	return (p);
}

#undef PROCESS_EXPAND

/**********************************************************************
 * Clean up process
 */

static void
process_delete(struct process *p)
{

	CHECK_OBJ_NOTNULL(p, PROCESS_MAGIC);
	AZ(pthread_mutex_destroy(&p->mtx));
	vtc_logclose(p->vl);
	free(p->name);
	free(p->dir);
	free(p->out);
	free(p->err);

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
 * Terminal emulation
 */

static void
process_term_clear(const struct process *p)
{
	int i;

	for (i = 0; i < NLINES; i++) {
		memset(p->term_c[i], ' ', NCOLS);
		p->term_c[i][NCOLS] = '\0';
	}
}

static void
process_screen_dump(const struct process *p)
{
	int i;

	for (i = 0; i < NLINES; i++)
		vtc_dump(p->vl, 3, "screen", p->term_c[i], NCOLS);
}

static void
process_escape(struct process *p, int c, int n)
{
	int i;

	for (i = 0; i < NTERMARG; i++)
		if (!p->term_arg[i])
			p->term_arg[i] = 1;
	switch(c) {
	case 'h':
		if (p->term_arg[0] <= NLINES && p->term_arg[1] <= NCOLS) {
			p->term_y = p->term_arg[0] - 1;
			p->term_x = p->term_arg[1] - 1;
		} else {
			vtc_log(p->vl, 4, "ANSI H %d %d WRONG",
			    p->term_arg[0], p->term_arg[1]);
		}
		break;
	case 'j':
		if (p->term_arg[0] == 2) {
			process_term_clear(p);
		} else {
			vtc_log(p->vl, 4, "ANSI J %d", p->term_arg[0]);
		}
		break;
	default:
		for (i = 0; i < n; i++)
			vtc_log(p->vl, 4, "ANSI arg %d",
			    p->term_arg[i]);
		vtc_log(p->vl, 4, "ANSI unk '%c'", c);
		break;
	}
}

static void
process_scroll(struct process *p)
{
	int i;
	char *l;

	l = p->term_c[0];
	for(i = 0; i < NLINES -1; i++)
		p->term_c[i] = p->term_c[i + 1];
	p->term_c[i] = l;
	memset(l, ' ', NCOLS);
}

static void
process_char(struct process *p, char c)
{
	assert(p->term_x < NCOLS);
	assert(p->term_y < NLINES);
	assert(p->term_state <= 3);
	switch (c) {
	case 0x00:
		break;
	case '\b':
		if (p->term_x > 0)
			p->term_x--;
		break;
	case '\t':
		while(++p->term_x % 8)
			continue;
		if (p->term_x >= NCOLS) {
			p->term_x = 0;
			process_char(p, '\n');
		}
		break;
	case '\n':
		if (p->term_y == NLINES - 1)
			process_scroll(p);
		else
			p->term_y++;
		break;
	case '\r':
		p->term_x = 0;
		break;
	default:
		if (c < ' ' || c > '~')
			vtc_log(p->vl, 4, "ANSI CTRL 0x%02x", c);
		else {
			p->term_c[p->term_y][p->term_x] = c;
			if (p->term_x == NCOLS - 1) {
				p->term_x = 0;
				process_char(p, '\n');
			} else {
				p->term_x++;
			}
		}
	}
}

static void
process_ansi(struct process *p, const char *b, const char *e)
{

	while (b < e) {
		assert(p->term_x < NCOLS);
		assert(p->term_y < NLINES);
		assert(p->term_state <= 3);
		switch (p->term_state) {
		case 0:
			if (*b == '\x1b')
				p->term_state = 1;
			else if (*(const uint8_t*)b == 0x9b)
				p->term_state = 2;
			else
				process_char(p, *b);
			b++;
			break;
		case 1:
			if (*b++ == '[') {
				p->term_state = 2;
			} else {
				vtc_log(p->vl, 4, "ANSI not [ 0x%x", b[-1]);
				p->term_state = 0;
			}
			break;
		case 2:
			p->term_ap = p->term_arg;
			memset(p->term_arg, 0, sizeof p->term_arg);
			p->term_state = 3;
			break;
		case 3:
			if (p->term_ap - p->term_arg >= NTERMARG) {
				vtc_log(p->vl, 4, "ANSI too many ;");
				p->term_state = 0;
				b++;
				continue;
			}
			if (isdigit(*b)) {
				*p->term_ap *= 10;
				*p->term_ap += *b++ - '0';
				continue;
			}
			if (*b == ';') {
				p->term_ap++;
				p->term_state = 3;
				b++;
				continue;
			}
			if (islower(*b)) {
				process_escape(p, *b++,
				    p->term_ap -  p->term_arg);
				p->term_state = 2;
			} else if (isupper(*b)) {
				process_escape(p, tolower(*b++),
				    p->term_ap -  p->term_arg);
				p->term_ap = p->term_arg;
				p->term_state = 0;
			} else {
				vtc_log(p->vl, 4, "ANSI non-letter %c", *b);
				p->term_state = 0;
				b++;
			}
			break;
		default:
			WRONG("Wrong ansi state");
		}
	}
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
	if (p->log == 1)
		(void)VLU_Feed(p->vlu_stdout, buf, i);
	else if (p->log == 2)
		vtc_dump(p->vl, 4, "stdout", buf, i);
	else if (p->log == 3)
		vtc_hexdump(p->vl, 4, "stdout", buf, i);
	(void)write(p->f_stdout, buf, i);
	process_ansi(p, buf, buf + i);
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
	vtc_dump(p->vl, 4, "stderr", buf, i);
	(void)write(p->f_stderr, buf, i);
	return (0);
}

static void *
process_thread(void *priv)
{
	struct process *p;
	struct rusage ru;
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

	r = wait4(p->pid, &p->status, 0, &ru);

	closefd(&p->f_stdout);
	closefd(&p->f_stderr);

	AZ(pthread_mutex_lock(&p->mtx));

	/* NB: We keep the other macros around */
	macro_undef(p->vl, p->name, "pid");
	p->pid = -1;

	vtc_log(p->vl, 2, "R 0x%04x Status: %04x (u %.6f s %.6f)",
	    r, p->status,
	    ru.ru_utime.tv_sec + 1e-6 * ru.ru_utime.tv_usec,
	    ru.ru_stime.tv_sec + 1e-6 * ru.ru_stime.tv_usec
	);

	AZ(pthread_mutex_unlock(&p->mtx));

#ifdef WCOREDUMP
	vtc_log(p->vl, 2, "Exit code: %04x sig %d exit %d core %d",
	    p->status, WTERMSIG(p->status), WEXITSTATUS(p->status),
	    WCOREDUMP(p->status));
#else
	vtc_log(p->vl, 2, "Exit code: %04x sig %d exit %d",
	    p->status, WTERMSIG(p->status), WEXITSTATUS(p->status));
#endif
	if (WEXITSTATUS(p->status) != p->expect_exit)
		vtc_fatal(p->vl, "Expected exit %d got %d",
			p->expect_exit, WEXITSTATUS(p->status));

	VEV_Destroy(&evb);
	if (p->log == 1) {
		VLU_Destroy(&p->vlu_stdout);
		VLU_Destroy(&p->vlu_stderr);
	}
	return (NULL);
}

static void
process_init_term(struct process *p, int fd)
{
	struct winsize ws;
	struct termios tt;
	int i;

	memset(&ws, 0, sizeof ws);
	ws.ws_row = 24;
	ws.ws_col = 80;
	i = ioctl(fd, TIOCSWINSZ, &ws);
	if (i)
		vtc_log(p->vl, 4, "TIOCWINSZ %d %s", i, strerror(errno));

	memset(&tt, 0, sizeof tt);
	tt.c_cflag = CREAD | CS8 | HUPCL;
	tt.c_iflag = BRKINT | ICRNL | IMAXBEL | IXON | IXANY;
	tt.c_lflag = ICANON | ISIG | IEXTEN;
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
	slave = open(slavename, O_RDWR);
	assert(slave >= 0);
#ifdef __sun
	if (ioctl(slave, I_PUSH, "ptem"))
		vtc_log(p->vl, 4, "PUSH ptem: %s", strerror(errno));
	if (ioctl(slave, I_PUSH, "ldterm"))
		vtc_log(p->vl, 4, "PUSH ldterm: %s", strerror(errno));
#endif

	process_init_term(p, slave);

	AZ(pipe(fd2));

	p->pid = fork();
	assert(p->pid >= 0);
	if (p->pid == 0) {
		AZ(setenv("TERM", "ansi", 1));
		AZ(unsetenv("TERMCAP"));
		assert(dup2(slave, STDIN_FILENO) == STDIN_FILENO);
		assert(dup2(slave, STDOUT_FILENO) == STDOUT_FILENO);
		assert(dup2(fd2[1], STDERR_FILENO) == STDERR_FILENO);
		VSUB_closefrom(STDERR_FILENO + 1);
		AZ(setpgid(0, 0));
		// Not using NULL because GCC is now even more demented...
		AZ(execl("/bin/sh", "/bin/sh", "-c", VSB_data(cl), (char*)0));
		exit(1);
	}
	vtc_log(p->vl, 3, "PID: %ld", (long)p->pid);
	VSB_destroy(&cl);

	closefd(&slave);
	p->fd_term = master;
	closefd(&fd2[1]);
	p->fd_stderr = fd2[0];
	macro_def(p->vl, p->name, "pid", "%ld", (long)p->pid);
	macro_def(p->vl, p->name, "dir", "%s", p->dir);
	macro_def(p->vl, p->name, "out", "%s", p->out);
	macro_def(p->vl, p->name, "err", "%s", p->err);
	p->hasthread = 1;
	AZ(pthread_create(&p->tp, NULL, process_thread, p));
}

/**********************************************************************
 * Wait for process thread to stop
 */

static void
process_wait(struct process *p)
{
	void *v;

	if (p->hasthread) {
		AZ(pthread_join(p->tp, &v));
		p->hasthread = 0;
	}
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

	AZ(pthread_mutex_lock(&p->mtx));
	pid = p->pid;
	AZ(pthread_mutex_unlock(&p->mtx));

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
	if (r < 0)
		vtc_fatal(p->vl, "Failed to write: %s (%d)",
		    strerror(errno), errno);
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
 *	process pNAME SPEC [-log] [-dump] [-hexdump] [-expect-exit N]
 *		[-start] [-run]
 *		[-write STRING] [-writeln STRING]
 *		[-kill STRING] [-stop] [-wait] [-close]
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
 * \-wait
 *	Wait for the process to finish.
 *
 * \-run
 *	Shorthand for -start -wait.
 *
 *	In most cases, if you just want to start a process and wait for it
 *	to finish, you can use the varnishtest ``shell`` command instead.
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
 * \-kill STRING
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
 * \-write STRING
 *	Write a string to the process' stdin.
 *
 * \-writeln STRING
 *	Same as -write followed by a newline (\\n).
 *
 * \-close
 *	Alias for "-kill HUP"
 *
 * \-screen_dump
 *	Dump the virtual screen into vtc_log
 *
 */

void
cmd_process(CMD_ARGS)
{
	struct process *p, *p2;

	(void)priv;
	(void)cmd;

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

	for (; *av != NULL; av++) {
		if (vtc_error)
			break;

		if (!strcmp(*av, "-start")) {
			process_start(p);
			continue;
		}
		if (!strcmp(*av, "-hexdump")) {
			if (p->hasthread)
				vtc_fatal(p->vl,
				    "Cannot dump a running process");
			p->log = 3;
			continue;
		}
		if (!strcmp(*av, "-dump")) {
			if (p->hasthread)
				vtc_fatal(p->vl,
				    "Cannot dump a running process");
			p->log = 2;
			continue;
		}
		if (!strcmp(*av, "-log")) {
			if (p->hasthread)
				vtc_fatal(p->vl,
				    "Cannot log a running process");
			p->log = 1;
			continue;
		}
		if (!strcmp(*av, "-expect-exit")) {
			p->expect_exit = strtoul(av[1], NULL, 0);
			av++;
			continue;
		}
		if (!strcmp(*av, "-wait")) {
			process_wait(p);
			continue;
		}
		if (!strcmp(*av, "-run")) {
			process_start(p);
			process_wait(p);
			continue;
		}
		if (!strcmp(*av, "-kill")) {
			process_kill(p, av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-stop")) {
			process_kill(p, "TERM");
			continue;
		}
		if (!strcmp(*av, "-write")) {
			process_write(p, av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-writeln")) {
			process_write(p, av[1]);
			process_write(p, "\n");
			av++;
			continue;
		}
		if (!strcmp(*av, "-screen_dump")) {
			process_screen_dump(p);
			continue;
		}
		if (!strcmp(*av, "-close")) {
			process_close(p);
			continue;
		}
		if (**av == '-')
			vtc_fatal(p->vl, "Unknown process argument: %s",
			    *av);
		REPLACE(p->spec, *av);
	}
}
