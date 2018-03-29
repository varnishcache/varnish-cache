/*-
 * Copyright (c) 2008-2017 Varnish Software AS
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

#include <sys/types.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>

#include "vtc.h"

#include "vct.h"
#include "vnum.h"
#include "vre.h"
#include "vtcp.h"

struct vsb *
vtc_hex_to_bin(struct vtclog *vl, const char *arg)
{
	struct vsb *vsb;
	unsigned sh = 4;
	unsigned c, b = 0;

	vsb = VSB_new_auto();
	AN(vsb);
	for (; *arg != '\0'; arg++) {
		if (vct_issp(*arg) || *arg == '\n')
			continue;
		c = (uint8_t)*arg;
		if (c >= '0' && c <= '9')
			b |= (c - 48U) << sh;
		else if (c >= 'A' && c <= 'F')
			b |= (c - 55U) << sh;
		else if (c >= 'a' && c <= 'f')
			b |= (c - 87U) << sh;
		else
			vtc_fatal(vl,"Illegal hex string");
		sh = 4 - sh;
		if (sh == 4) {
			VSB_putc(vsb, b);
			b = 0;
		}
	}
	if (sh != 4)
		VSB_putc(vsb, b);
	AZ(VSB_finish(vsb));
	return (vsb);
}

void
vtc_expect(struct vtclog *vl,
    const char *olhs, const char *lhs,
    const char *cmp,
    const char *orhs, const char *rhs)
{
	vre_t *vre;
	const char *error;
	int erroroffset;
	int i, j, retval = -1;
	double fl, fr;

	j = lhs == NULL || rhs == NULL;
	if (lhs == NULL)
		lhs = "<undef>";
	if (rhs == NULL)
		rhs = "<undef>";

	if (!strcmp(cmp, "~") || !strcmp(cmp, "!~")) {
		vre = VRE_compile(rhs, 0, &error, &erroroffset);
		if (vre == NULL)
			vtc_fatal(vl, "REGEXP error: %s (@%d) (%s)",
			    error, erroroffset, rhs);
		i = VRE_exec(vre, lhs, strlen(lhs), 0, 0, NULL, 0, 0);
		retval = (i >= 0 && *cmp == '~') || (i < 0 && *cmp == '!');
		VRE_free(&vre);
	} else if (!strcmp(cmp, "==")) {
		retval = strcmp(lhs, rhs) == 0;
	} else if (!strcmp(cmp, "!=")) {
		retval = strcmp(lhs, rhs) != 0;
	} else if (j) {
		// fail inequality comparisons if either side is undef'ed
		retval = 0;
	} else {
		fl = VNUM(lhs);
		fr = VNUM(rhs);
		if (!strcmp(cmp, "<"))
			retval = isless(fl, fr);
		else if (!strcmp(cmp, ">"))
			retval = isgreater(fl, fr);
		else if (!strcmp(cmp, "<="))
			retval = islessequal(fl, fr);
		else if (!strcmp(cmp, ">="))
			retval = isgreaterequal(fl, fr);
	}

	if (retval == -1)
		vtc_fatal(vl,
		    "EXPECT %s (%s) %s %s (%s) test not implemented",
		    olhs, lhs, cmp, orhs, rhs);
	else if (retval == 0)
		vtc_fatal(vl, "EXPECT %s (%s) %s \"%s\" failed",
		    olhs, lhs, cmp, rhs);
	else
		vtc_log(vl, 4, "EXPECT %s (%s) %s \"%s\" match",
		    olhs, lhs, cmp, rhs);
}

/**********************************************************************
 * Wait for a subprocess.
 *
 * if expect_signal > 0, the process must die on that signal.
 * if expect_signal < 0, dying on that signal is allowed, but not required.
 * if allow_core > 0, a coredump is allowed, but not required.
 * otherwise, the process must die on exit(expect_status)
 */

#ifndef WCOREDUMP
#  define WCOREDUMP(s) (-1)
#endif

void
vtc_wait4(struct vtclog *vl, long pid,
    int expect_status, int expect_signal, int allow_core)
{
	int status, r;
	struct rusage ru;

	r = wait4(pid, &status, 0, &ru);
	if (r < 0)
		vtc_fatal(vl, "wait4 failed on pid %ld: %s",
		    pid, strerror(errno));
	assert(r == pid);
	vtc_log(vl, 2, "WAIT4 pid=%ld status=0x%04x (user %.6f sys %.6f)",
	    pid, status,
	    ru.ru_utime.tv_sec + 1e-6 * ru.ru_utime.tv_usec,
	    ru.ru_stime.tv_sec + 1e-6 * ru.ru_stime.tv_usec
	);

	if (WIFEXITED(status) && expect_signal <= 0 &&
	    WEXITSTATUS(status) == expect_status)
		return;

	if (expect_signal < 0)
		expect_signal = -expect_signal;

	if (WIFSIGNALED(status) && WCOREDUMP(status) <= allow_core &&
	    WTERMSIG(status) == expect_signal)
		return;
	vtc_log(vl, 1, "Expected exit: 0x%x signal: %d core: %d",
	    expect_status, expect_signal, allow_core);
	vtc_fatal(vl, "Bad exit status: 0x%04x exit 0x%x signal %d core %d",
	    status,
	    WEXITSTATUS(status),
	    WIFSIGNALED(status) ? WTERMSIG(status) : 0,
	    WCOREDUMP(status));
}

void *
vtc_record(struct vtclog *vl, int fd, struct vsb *vsb)
{
	char buf[65536];
	struct pollfd fds[1];
	int i;

	(void)VTCP_nonblocking(fd);
	while (1) {
		memset(fds, 0, sizeof fds);
		fds->fd = fd;
		fds->events = POLLIN;
		i = poll(fds, 1, 10000);
		if (i == 0)
			continue;
		if (fds->revents & POLLIN) {
			i = read(fd, buf, sizeof buf - 1);
			if (i > 0) {
				if (vsb != NULL)
					VSB_bcat(vsb, buf, i);
				buf[i] = '\0';
				vtc_dump(vl, 3, "debug", buf, -2);
			}
		}
		if (fds->revents & (POLLERR|POLLHUP)) {
			vtc_log(vl, 4, "STDOUT poll 0x%x", fds->revents);
			break;
		}
	}
	return (NULL);
}

