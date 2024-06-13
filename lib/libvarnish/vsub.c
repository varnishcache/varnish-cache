/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * Run stuff in a child process
 */

#include "config.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>		// Solaris closefrom(3c)
#include <string.h>
#include <unistd.h>		// BSD/Linux close_range(2)
#ifndef HAVE_CLOSEFROM
#  include <dirent.h>
#endif

#include "vdef.h"

#include "vapi/vsig.h"

#include "vas.h"
#include "vfil.h"
#include "vlu.h"
#include "vsb.h"
#include "vsub.h"

struct vsub_priv {
	const char	*name;
	struct vsb	*sb;
	int		lines;
	int		maxlines;
};

void
VSUB_closefrom(int fd)
{

	assert(fd >= 0);

#ifdef HAVE_CLOSEFROM
	closefrom(fd);
	return;
#else
#  ifdef HAVE_WORKING_CLOSE_RANGE
	if (close_range(fd, ~0U, 0) == 0)
		return;
#  endif
	char buf[128];
	int i;
	DIR *d;
	struct dirent *de;
	char *p;

	bprintf(buf, "/proc/%d/fd/", getpid());
	d = opendir(buf);
	if (d != NULL) {
		while (1) {
			de = readdir(d);
			if (de == NULL)
				break;
			i = strtoul(de->d_name, &p, 10);
			if (*p != '\0' || i <= fd || i == dirfd(d))
				continue;
			(void)close(i);
		}
		AZ(closedir(d));
	} else {
		/* NB: Very slow if called with a high FD limit */
		i = sysconf(_SC_OPEN_MAX);
		assert(i > 0);
		for (; i > fd; i--)
			(void)close(i);
	}
#endif
}

static int
vsub_vlu(void *priv, const char *str)
{
	struct vsub_priv *sp;

	sp = priv;
	if (!sp->lines++)
		VSB_printf(sp->sb, "Message from %s:\n", sp->name);
	if (sp->maxlines < 0 || sp->lines <= sp->maxlines)
		VSB_printf(sp->sb, "%s\n", str);
	return (0);
}

/* returns an exit code */
unsigned
VSUB_run(struct vsb *sb, vsub_func_f *func, void *priv, const char *name,
    int maxlines)
{
	int rv, p[2], status;
	pid_t pid;
	struct vsub_priv sp;

	sp.sb = sb;
	sp.name = name;
	sp.lines = 0;
	sp.maxlines = maxlines;

	if (pipe(p) < 0) {
		VSB_printf(sb, "Starting %s: pipe() failed: %s",
		    name, strerror(errno));
		return (1);
	}
	assert(p[0] > STDERR_FILENO);
	assert(p[1] > STDERR_FILENO);
	if ((pid = fork()) < 0) {
		VSB_printf(sb, "Starting %s: fork() failed: %s",
		    name, strerror(errno));
		closefd(&p[0]);
		closefd(&p[1]);
		return (1);
	}
	if (pid == 0) {
		VFIL_null_fd(STDIN_FILENO);
		assert(dup2(p[1], STDOUT_FILENO) == STDOUT_FILENO);
		assert(dup2(p[1], STDERR_FILENO) == STDERR_FILENO);
		/* Close all other fds */
		VSUB_closefrom(STDERR_FILENO + 1);
		func(priv);
		/*
		 * func should either exec or exit, so getting here should be
		 * treated like an assertion failure - except that we don't know
		 * if it's safe to trigger an actual assertion
		 */
		_exit(4);
	}
	closefd(&p[1]);
	(void)VLU_File(p[0], vsub_vlu, &sp, 0);
	closefd(&p[0]);
	if (sp.maxlines >= 0 && sp.lines > sp.maxlines)
		VSB_printf(sb, "[%d lines truncated]\n",
		    sp.lines - sp.maxlines);
	do {
		rv = waitpid(pid, &status, 0);
		if (rv < 0 && errno != EINTR) {
			VSB_printf(sb, "Running %s: waitpid() failed: %s\n",
			    name, strerror(errno));
			return (1);
		}
	} while (rv < 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		rv = -1;
		VSB_printf(sb, "Running %s failed", name);
		if (WIFEXITED(status)) {
			rv = WEXITSTATUS(status);
			VSB_printf(sb, ", exited with %d", rv);
		}
		if (WIFSIGNALED(status)) {
			rv = 2;
			VSB_printf(sb, ", signal %d", WTERMSIG(status));
		}
		if (WCOREDUMP(status))
			VSB_cat(sb, ", core dumped");
		VSB_cat(sb, "\n");
		assert(rv != -1);
		return (rv);
	}
	return (0);
}
