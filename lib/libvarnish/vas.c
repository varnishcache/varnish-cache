/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2016 Varnish Software AS
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
 * This is the default backend function for libvarnish' assert facilities.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vdef.h"
#include "vbt.h"

#include "vas.h"

const char *
VAS_errtxt(int e)
{
	const char *p;
	int oerrno = errno;

	p = strerror(e);
	if (p != NULL)
		return (p);

	errno = oerrno;
	return ("strerror(3) returned NULL");
}

vas_f *VAS_Fail_Func v_noreturn_;

static void
vas_default(const char *func, const char *file, int line,
    const char *cond, enum vas_e kind)
{
	int err = errno;
	char buf[4096];

	if (kind == VAS_MISSING) {
		fprintf(stderr,
		    "Missing error handling code in %s(), %s line %d:\n"
		    "  Condition(%s) not true.\n",
		    func, file, line, cond);
	} else if (kind == VAS_INCOMPLETE) {
		fprintf(stderr,
		    "Incomplete code in %s(), %s line %d:\n",
		    func, file, line);
	} else if (kind == VAS_WRONG) {
		fprintf(stderr,
		    "Wrong turn in %s(), %s line %d: %s\n",
		    func, file, line, cond);
	} else {
		fprintf(stderr,
		    "Assert error in %s(), %s line %d:\n"
		    "  Condition(%s) not true.\n",
		    func, file, line, cond);
	}
	if (err) {
		fprintf(stderr,
		    "  errno = %d (%s)\n", err, strerror(err));
	}

	if (VBT_dump(sizeof buf, buf) < 0) {
		bprintf(buf, "Failed to print backtrace: %d (%s)\n",
		    errno, strerror(errno));
	}

	fprintf(stderr, "%s", buf);
}

void v_noreturn_
VAS_Fail(const char *func, const char *file, int line,
    const char *cond, enum vas_e kind)
{

	if (VAS_Fail_Func != NULL)
		VAS_Fail_Func(func, file, line, cond, kind);
	else
		vas_default(func, file, line, cond, kind);
	abort();
}
