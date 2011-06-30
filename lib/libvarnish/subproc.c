/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 * Run stuff in a child process
 */

#include "config.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include <sys/wait.h>

#include "vsb.h"
#include "vlu.h"
#include "libvarnish.h"

struct sub_priv {
	const char	*name;
	struct vsb	*sb;
	int		lines;
	int		maxlines;
};

static int
sub_vlu(void *priv, const char *str)
{
	struct sub_priv *sp;

	sp = priv;
	if (!sp->lines++)
		VSB_printf(sp->sb, "Message from %s:\n", sp->name);
	if (sp->maxlines < 0 || sp->lines <= sp->maxlines)
		VSB_printf(sp->sb, "%s\n", str);
	return (0);
}

int
SUB_run(struct vsb *sb, sub_func_f *func, void *priv, const char *name,
    int maxlines)
{
	int rv, p[2], sfd, status;
	pid_t pid;
	struct vlu *vlu;
	struct sub_priv sp;

	sp.sb = sb;
	sp.name = name;
	sp.lines = 0;
	sp.maxlines = maxlines;

	if (pipe(p) < 0) {
		VSB_printf(sb, "Starting %s: pipe() failed: %s",
		    name, strerror(errno));
		return (-1);
	}
	assert(p[0] > STDERR_FILENO);
	assert(p[1] > STDERR_FILENO);
	if ((pid = fork()) < 0) {
		VSB_printf(sb, "Starting %s: fork() failed: %s",
		    name, strerror(errno));
		AZ(close(p[0]));
		AZ(close(p[1]));
		return (-1);
	}
	if (pid == 0) {
		AZ(close(STDIN_FILENO));
		assert(open("/dev/null", O_RDONLY) == STDIN_FILENO);
		assert(dup2(p[1], STDOUT_FILENO) == STDOUT_FILENO);
		assert(dup2(p[1], STDERR_FILENO) == STDERR_FILENO);
		/* Close all other fds */
		for (sfd = STDERR_FILENO + 1; sfd < 100; sfd++)
			(void)close(sfd);
		func(priv);
		_exit(1);
	}
	AZ(close(p[1]));
	vlu = VLU_New(&sp, sub_vlu, 0);
	while (!VLU_Fd(p[0], vlu))
		continue;
	AZ(close(p[0]));
	VLU_Destroy(vlu);
	if (sp.maxlines >= 0 && sp.lines > sp.maxlines)
		VSB_printf(sb, "[%d lines truncated]\n",
		    sp.lines - sp.maxlines);
	do {
		rv = waitpid(pid, &status, 0);
		if (rv < 0 && errno != EINTR) {
			VSB_printf(sb, "Running %s: waitpid() failed: %s\n",
			    name, strerror(errno));
			return (-1);
		}
	} while (rv < 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		VSB_printf(sb, "Running %s failed", name);
		if (WIFEXITED(status))
			VSB_printf(sb, ", exit %d", WEXITSTATUS(status));
		if (WIFSIGNALED(status))
			VSB_printf(sb, ", signal %d", WTERMSIG(status));
		if (WCOREDUMP(status))
			VSB_printf(sb, ", core dumped");
		VSB_printf(sb, "\n");
		return (-1);
	}
	return (0);
}
