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
 */

#include "config.h"

#include <sys/resource.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vtc.h"
#include "vlu.h"
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
	int			fd_to;
	int			log;
	int			fd_from;
	pid_t			pid;

	pthread_mutex_t		mtx;
	pthread_t		tp;
	unsigned		hasthread;
	int			status;
};

static VTAILQ_HEAD(, process)	processes =
    VTAILQ_HEAD_INITIALIZER(processes);

/**********************************************************************
 * Allocate and initialize a process
 */

#define PROCESS_EXPAND(field, format, ...)		\
	do {						\
		bprintf(buf, format, __VA_ARGS__);	\
		vsb = macro_expand(p->vl, buf);		\
		AN(vsb);				\
		p->field = strdup(VSB_data(vsb));	\
		AN(p->field);				\
		VSB_destroy(&vsb);			\
	} while (0)

static struct process *
process_new(const char *name)
{
	struct process *p;
	struct vsb *vsb;
	char buf[1024];

	ALLOC_OBJ(p, PROCESS_MAGIC);
	AN(p);
	REPLACE(p->name, name);
	AZ(pthread_mutex_init(&p->mtx, NULL));

	p->vl = vtc_logopen(name);
	AN(p->vl);

	PROCESS_EXPAND(dir, "${tmpdir}/%s", name);
	PROCESS_EXPAND(out, "${tmpdir}/%s/stdout", name);
	PROCESS_EXPAND(err, "${tmpdir}/%s/stderr", name);

	bprintf(buf, "rm -rf %s ; mkdir -p %s ; touch %s %s",
	    p->dir, p->dir, p->out, p->err);
	AZ(system(buf));

	p->fd_to = -1;
	p->fd_from = -1;

	VTAILQ_INSERT_TAIL(&processes, p, list);
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
 * Start the process thread
 */

static int
process_vlu_func(void *priv, const char *l)
{
	struct process *p;

	CAST_OBJ_NOTNULL(p, priv, PROCESS_MAGIC);
	vtc_dump(p->vl, 4, "output", l, -1);
	return (0);
}

static void *
process_thread(void *priv)
{
	struct process *p;
	struct rusage ru;
	int r;
	struct vlu *vlu;

	CAST_OBJ_NOTNULL(p, priv, PROCESS_MAGIC);
	if (p->fd_from > 0) {
		vlu = VLU_New(p, process_vlu_func, 1024);
		while (!VLU_Fd(p->fd_from, vlu))
			continue;
	}
	r = wait4(p->pid, &p->status, 0, &ru);


	AZ(pthread_mutex_lock(&p->mtx));

	if (p->fd_to >= 0)
		closefd(&p->fd_to);

	/* NB: We keep the other macros around */
	macro_undef(p->vl, p->name, "pid");
	p->pid = -1;

	vtc_log(p->vl, 2, "R 0x%04x Status: %04x (u %.6f s %.6f)",
	    r, p->status,
	    ru.ru_utime.tv_sec + 1e-6 * ru.ru_utime.tv_usec,
	    ru.ru_stime.tv_sec + 1e-6 * ru.ru_stime.tv_usec
	);

	AZ(pthread_mutex_unlock(&p->mtx));

	if (WIFEXITED(p->status) && WEXITSTATUS(p->status) == 0)
		return (NULL);
#ifdef WCOREDUMP
	vtc_log(p->vl, 2, "Bad exit code: %04x sig %d exit %d core %d",
	    p->status, WTERMSIG(p->status), WEXITSTATUS(p->status),
	    WCOREDUMP(p->status));
#else
	vtc_log(p->vl, 2, "Bad exit code: %04x sig %d exit %d",
	    p->status, WTERMSIG(p->status), WEXITSTATUS(p->status));
#endif

	return (NULL);
}

static void
process_start(struct process *p)
{
	struct vsb *cl;
	int out_fd, err_fd;
	int fds[2];
	int fdt[2] = { -1, -1 };

	CHECK_OBJ_NOTNULL(p, PROCESS_MAGIC);
	if (p->hasthread)
		vtc_fatal(p->vl, "Already running, -wait first");

	vtc_log(p->vl, 4, "CMD: %s", p->spec);

	cl = macro_expand(p->vl, p->spec);
	AN(cl);
	AZ(pipe(fds));
	if (p->log) {
		AZ(pipe(fdt));
		out_fd = fdt[1];
		err_fd = fdt[1];
	} else {
		out_fd = open(p->out, O_WRONLY|O_APPEND);
		assert(out_fd >= 0);
		err_fd = open(p->err, O_WRONLY|O_APPEND);
		assert(err_fd >= 0);
	}
	p->pid = fork();
	assert(p->pid >= 0);
	if (p->pid == 0) {
		assert(dup2(fds[0], 0) == 0);
		assert(dup2(out_fd, 1) == 1);
		assert(dup2(err_fd, 2) == 2);
		VSUB_closefrom(STDERR_FILENO + 1);
		AZ(setpgid(0, 0));
		AZ(execl("/bin/sh", "/bin/sh", "-c", VSB_data(cl),
		    (char *)NULL));
		exit(1);
	}
	vtc_log(p->vl, 3, "PID: %ld", (long)p->pid);
	macro_def(p->vl, p->name, "pid", "%ld", (long)p->pid);
	macro_def(p->vl, p->name, "dir", "%s", p->dir);
	macro_def(p->vl, p->name, "out", "%s", p->out);
	macro_def(p->vl, p->name, "err", "%s", p->err);
	closefd(&fds[0]);
	p->fd_to = fds[1];
	if (p->log) {
		closefd(&fdt[1]);
		p->fd_from = fdt[0];
	} else {
		closefd(&out_fd);
		closefd(&err_fd);
	}
	VSB_destroy(&cl);
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
	r = write(p->fd_to, text, len);
	if (r < 0)
		vtc_fatal(p->vl, "Failed to write: %s (%d)",
		    strerror(errno), errno);
}

static void
process_close(struct process *p)
{

	if (!p->hasthread)
		vtc_fatal(p->vl, "Cannot close a non-running process");

	AZ(pthread_mutex_lock(&p->mtx));
	if (p->fd_to >= 0)
		closefd(&p->fd_to);
	AZ(pthread_mutex_unlock(&p->mtx));
}

/* SECTION: process process
 *
 * Run a process in the background with stdout and stderr redirected to
 * ${pNAME_out} and ${pNAME_err}, both located in ${pNAME_dir}::
 *
 *	process pNAME SPEC [-log] [-start] [-wait] [-run] [-kill STRING] \
 *		[-stop] [-write STRING] [-writeln STRING] [-close]
 *
 * pNAME
 *	Name of the process. It must start with 'p'.
 *
 * SPEC
 *	The command(s) to run in this process.
 *
 * \-log
 *	Log stdout/stderr with vtc_dump(). Must be before -start/-run.
 *
 * \-start
 *	Start the process.
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
 *	Close the process' stdin.
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
		if (!strcmp(*av, "-log")) {
			if (p->hasthread)
				vtc_fatal(p->vl,
				    "Cannot log a running process");
			p->log = 1;
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
