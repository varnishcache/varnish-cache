/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2013 Varnish Software AS
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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
 * Common functions for the utilities
 */

#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "compat/daemon.h"
#include "vapi/vsm.h"
#include "vapi/vsc.h"
#include "vapi/vsl.h"
#include "vas.h"
#include "miniobj.h"

#include "vut.h"

void
VUT_Error(int status, const char *fmt, ...)
{
	va_list ap;

	AN(fmt);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");

	if (status)
		exit(status);
}

struct VUT*
VUT_New(void)
{
	struct VUT *vut;

	vut = calloc(1, sizeof *vut);
	AN(vut);
	vut->g_arg = VSL_g_vxid;

	return (vut);
}

void
VUT_Delete(struct VUT **pvut)
{
	struct VUT *vut;

	AN(pvut);
	vut = *pvut;
	*pvut = NULL;
	AN(vut);

	free(vut->r_arg);

	free(vut);
}

int
VUT_g_Arg(struct VUT *vut, const char *arg)
{

	vut->g_arg = VSLQ_Name2Grouping(arg, -1);
	if (vut->g_arg == -2)
		VUT_Error(1, "Ambigous grouping type: %s", arg);
	else if (vut->g_arg < 0)
		VUT_Error(1, "Unknown grouping type: %s", arg);
	return (1);
}

int
VUT_Arg(struct VUT *vut, int opt, const char *arg)
{
	switch (opt) {
	case 'd': vut->d_opt = 1; return (1);
	case 'g': return (VUT_g_Arg(vut, arg));
	case 'r': REPLACE(vut->r_arg, arg); return (1);
	default: return (0);
	}
}
