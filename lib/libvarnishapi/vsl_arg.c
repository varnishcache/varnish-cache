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
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "miniobj.h"
#include "vas.h"
#include "vdef.h"

#include "vapi/vsl.h"
#include "vapi/vsm.h"
#include "vbm.h"
#include "vre.h"
#include "vsl_api.h"
#include "vsm_api.h"

/*--------------------------------------------------------------------
 * Look up a tag
 *   0..255	tag number
 *   -1		no tag matches
 *   -2		multiple tags match
 */

int
VSL_Name2Tag(const char *name, int l)
{
	int i, n;

	if (l == -1)
		l = strlen(name);
	n = -1;
	for (i = 0; i < 256; i++) {
		if (VSL_tags[i] != NULL &&
		    !strncasecmp(name, VSL_tags[i], l)) {
			if (strlen(VSL_tags[i]) == l) {
				/* Exact match */
				return (i);
			}
			if (n == -1)
				n = i;
			else
				n = -2;
		}
	}
	return (n);
}

static int
vsl_ix_arg(struct VSL_data *vsl, int opt, const char *arg)
{
	int i, l;
	const char *b, *e;

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	/* If first option is 'i', set all bits for supression */
	if (opt == 'i' && !(vsl->flags & F_SEEN_ix))
		for (i = 0; i < 256; i++)
			vbit_set(vsl->vbm_supress, i);
	vsl->flags |= F_SEEN_ix;

	for (b = arg; *b; b = e) {
		while (isspace(*b))
			b++;
		e = strchr(b, ',');
		if (e == NULL)
			e = strchr(b, '\0');
		l = e - b;
		if (*e == ',')
			e++;
		while (isspace(b[l - 1]))
			l--;
		i = VSL_Name2Tag(b, l);
		if (i >= 0) {
			if (opt == 'x')
				vbit_set(vsl->vbm_supress, i);
			else
				vbit_clr(vsl->vbm_supress, i);
		} else if (i == -2) {
			return (vsl_diag(vsl,
			    "-%c: \"%*.*s\" matches multiple tags\n",
			    (char)opt, l, l, b));
		} else {
			return (vsl_diag(vsl,
			    "-%c: Could not match \"%*.*s\" to any tag\n",
			    (char)opt, l, l, b));
		}
	}
	return (1);
}

int
VSL_Arg(struct VSL_data *vsl, int opt, const char *arg)
{
	switch (opt) {
	case 'i': case'x': return (vsl_ix_arg(vsl, opt, arg));
	default:
		return (0);
	}
}
