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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vtc.h"
#include "vsub.h"

struct process {
	unsigned		magic;
#define PROCESS_MAGIC		0x1617b43e
	char			*name;
	struct vtclog		*vl;
	VTAILQ_ENTRY(process)	list;

	char			*spec;
	char			*workdir;
	char			*outdir;
	char			*out;
	char			*err;
	int			fds[2];
	pid_t			pid;

	pthread_t		tp;
	unsigned		running;
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
		VSB_delete(vsb);			\
	} while (0)

static struct process *
process_new(const char *name)
{
	struct process *p;
	struct vsb *vsb;
	char buf[1024];

	AN(name);
	ALLOC_OBJ(p, PROCESS_MAGIC);
	AN(p);
	REPLACE(p->name, name);

	p->vl = vtc_logopen(name);
	AN(p->vl);

	PROCESS_EXPAND(workdir, "%s", "${pwd}");
	PROCESS_EXPAND(outdir, "${tmpdir}/%s", name);
	PROCESS_EXPAND(out, "${tmpdir}/%s/stdout", name);
	PROCESS_EXPAND(err, "${tmpdir}/%s/stderr", name);

	bprintf(buf, "rm -rf %s ; mkdir -p %s ; touch %s %s",
	    p->outdir, p->outdir, p->out, p->err);
	AZ(system(buf));

	p->fds[0] = -1;
	p->fds[1] = -1;

	if (*p->name != 'p')
		vtc_log(p->vl, 0, "Process name must start with 'p'");

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
	vtc_logclose(p->vl);
	free(p->name);
	free(p->workdir);
	free(p->outdir);
	free(p->out);
	free(p->err);

	/*
	 * We do not delete the outdir, it may contain useful stdout
	 * and stderr files.
	 */

	/* XXX: MEMLEAK (?) */
	FREE_OBJ(p);
}

/**********************************************************************
 * Start the process thread
 */

static void *
process_thread(void *priv)
{
	struct process *p;
	struct rusage ru;
	int r;

	CAST_OBJ_NOTNULL(p, priv, PROCESS_MAGIC);
	r = wait4(p->pid, &p->status, 0, &ru);
	macro_undef(p->vl, p->name, "pid");
	p->pid = 0;
	p->running = 0;
	vtc_log(p->vl, 2, "R %d Status: %04x (u %.6f s %.6f)", r, p->status,
	    ru.ru_utime.tv_sec + 1e-6 * ru.ru_utime.tv_usec,
	    ru.ru_stime.tv_sec + 1e-6 * ru.ru_stime.tv_usec
	);

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

	(void)close(p->fds[1]);
	p->fds[1] = -1;

	return (NULL);
}

static void
process_start(struct process *p)
{
	struct vsb *cl;
	int out_fd, err_fd;

	CHECK_OBJ_NOTNULL(p, PROCESS_MAGIC);

	vtc_log(p->vl, 4, "CMD: %s", p->spec);

	cl = macro_expand(p->vl, p->spec);
	AN(cl);
	AZ(pipe(p->fds));
	out_fd = open(p->out, O_WRONLY|O_APPEND);
	assert(out_fd >= 0);
	err_fd = open(p->err, O_WRONLY|O_APPEND);
	assert(err_fd >= 0);
	p->pid = fork();
	assert(p->pid >= 0);
	p->running = 1;
	if (p->pid == 0) {
		assert(dup2(p->fds[0], 0) == 0);
		assert(dup2(out_fd, 1) == 1);
		assert(dup2(err_fd, 2) == 2);
		VSUB_closefrom(STDERR_FILENO + 1);
		AZ(execl("/bin/sh", "/bin/sh", "-c", VSB_data(cl), (char*)0));
		exit(1);
	}
	vtc_log(p->vl, 3, "PID: %ld", (long)p->pid);
	macro_def(p->vl, p->name, "pid", "%ld", (long)p->pid);
	AZ(close(p->fds[0]));
	AZ(close(out_fd));
	AZ(close(err_fd));
	p->fds[0] = -1;
	VSB_delete(cl);
	AZ(pthread_create(&p->tp, NULL, process_thread, p));
}

/**********************************************************************
 * Wait for process thread to stop
 */

static void
process_wait(const struct process *p)
{
	void *v;

	if (p->running && p->pid)
		AZ(pthread_join(p->tp, &v));
}

/**********************************************************************
 * Send a signal to a process
 */

static void
process_kill(const struct process *p, const char *sig)
{
	int s;
	char buf[64];

	CHECK_OBJ_NOTNULL(p, PROCESS_MAGIC);
	AN(sig);

	if (!p->running || !p->pid)
		vtc_log(p->vl, 0, "Cannot signal a non-running process");

	bprintf(buf, "kill -%s %d", sig, p->pid);
	vtc_log(p->vl, 4, "CMD: %s", buf);

	s = system(buf);
	if (s != 0)
		vtc_log(p->vl, 0, "Failed to send signal (exit status: %d)", s);
}

static inline void
process_stop(const struct process *p)
{

	process_kill(p, "TERM");
}

static inline void
process_terminate(const struct process *p)
{

	process_kill(p, "TERM");
	sleep(1);
	if (p->running && p->pid)
		process_kill(p, "KILL");
}

/**********************************************************************
 * Write to a process' stdin
 */

static void
process_write(const struct process *p, const char *text)
{
	int r, len;

	if (!p->running || !p->pid)
		vtc_log(p->vl, 0, "Cannot write to a non-running process");

	len = strlen(text);
	vtc_log(p->vl, 4, "Writing %d bytes", len);
	r = write(p->fds[1], text, len);
	if (r < 0)
		vtc_log(p->vl, 0, "Failed to write: %s (%d)",
		    strerror(errno), errno);
}

static void
process_close(struct process *p)
{

	if (!p->running || !p->pid)
		vtc_log(p->vl, 0, "Cannot close on a non-running process");

	(void)close(p->fds[1]);
	p->fds[1] = -1;
}

/**********************************************************************
 * Process command dispatch
 */

void
cmd_process(CMD_ARGS)
{
	struct process *p, *p2;

	(void)priv;
	(void)cmd;
	(void)vl;

	if (av == NULL) {
		/* Reset and free */
		VTAILQ_FOREACH_SAFE(p, &processes, list, p2) {
			if (p->running && p->pid)
				process_terminate(p);
			VTAILQ_REMOVE(&processes, p, list);
			process_delete(p);
		}
		return;
	}

	AZ(strcmp(av[0], "process"));
	av++;

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
		if (!strcmp(*av, "-wait")) {
			process_wait(p);
			continue;
		}
		if (!strcmp(*av, "-kill")) {
			process_kill(p, av[1]);
			av++;
			continue;
		}
		if (!strcmp(*av, "-stop")) {
			process_stop(p);
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
			vtc_log(p->vl, 0, "Unknown process argument: %s", *av);
		REPLACE(p->spec, *av);
	}
}
