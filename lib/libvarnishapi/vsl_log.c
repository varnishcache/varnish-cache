/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Redpill Linpro AS
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

#include "svnid.h"
SVNID("$Id$")

#include <sys/types.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vas.h"
#include "shmlog.h"
#include "vre.h"
#include "vbm.h"
#include "vqueue.h"
#include "miniobj.h"
#include "varnishapi.h"

#include "vslapi.h"
#include "vmb.h"

static int vsl_nextlog(struct VSL_data *vd, uint32_t **pp);

/*--------------------------------------------------------------------*/

const char *VSL_tags[256] = {
#define SLTM(foo)       [SLT_##foo] = #foo,
#include "shmlog_tags.h"
#undef SLTM
};

/*--------------------------------------------------------------------*/

void
VSL_Select(const struct VSL_data *vd, unsigned tag)
{

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);
	vbit_set(vd->vbm_select, tag);
}


/*--------------------------------------------------------------------*/

void
VSL_NonBlocking(struct VSL_data *vd, int nb)
{
	if (nb)
		vd->flags |= F_NON_BLOCKING;
	else
		vd->flags &= ~F_NON_BLOCKING;
}

/*--------------------------------------------------------------------*/

static int
vsl_nextlog(struct VSL_data *vd, uint32_t **pp)
{
	unsigned w, l;
	uint32_t t;
	int i;

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);
	if (vd->r_fd != -1) {
		assert(vd->rbuflen >= 8);
		i = read(vd->r_fd, vd->rbuf, 8);
		if (i != 8)
			return (-1);
		l = 2 + VSL_WORDS(VSL_LEN(vd->rbuf));
		if (vd->rbuflen < l) {
			l += 256;
			vd->rbuf = realloc(vd->rbuf, l * 4);
			assert(vd->rbuf != NULL);
			vd->rbuflen = l;
		}
		i = read(vd->r_fd, vd->rbuf + 2, l * 4 - 8);
		if (i != l)
			return (-1);
		*pp = vd->rbuf;
		return (1);
	}
	for (w = 0; w < TIMEOUT_USEC;) {
		t = *vd->log_ptr;

		if (t == VSL_WRAPMARKER ||
		    (t == VSL_ENDMARKER && vd->last_seq != vd->log_start[0])) {
			vd->log_ptr = vd->log_start + 1;
			vd->last_seq = vd->log_start[0];
			VRMB();
			continue;
		}
		if (t == VSL_ENDMARKER) {
			if (vd->flags & F_NON_BLOCKING)
				return (-1);
			w += SLEEP_USEC;
			AZ(usleep(SLEEP_USEC));
			continue;
		}
		*pp = (void*)(uintptr_t)vd->log_ptr; /* Loose volatile */
		vd->log_ptr = VSL_NEXT(vd->log_ptr);
		return (1);
	}
	*pp = NULL;
	return (0);
}

int
VSL_NextLog(struct VSL_data *vd, uint32_t **pp)
{
	uint32_t *p;
	unsigned char t;
	unsigned u;
	int i;

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);
	while (1) {
		i = vsl_nextlog(vd, &p);
		if (i != 1)
			return (i);
		u = VSL_ID(p);
		t = VSL_TAG(p);
		switch(t) {
		case SLT_SessionOpen:
		case SLT_ReqStart:
			vbit_set(vd->vbm_client, u);
			vbit_clr(vd->vbm_backend, u);
			break;
		case SLT_BackendOpen:
		case SLT_BackendXID:
			vbit_clr(vd->vbm_client, u);
			vbit_set(vd->vbm_backend, u);
			break;
		default:
			break;
		}
		if (vd->skip) {
			--vd->skip;
			continue;
		} else if (vd->keep) {
			if (--vd->keep == 0)
				return (-1);
		}

		if (vbit_test(vd->vbm_select, t)) {
			*pp = p;
			return (1);
		}
		if (vbit_test(vd->vbm_supress, t))
			continue;
		if (vd->b_opt && !vbit_test(vd->vbm_backend, u))
			continue;
		if (vd->c_opt && !vbit_test(vd->vbm_client, u))
			continue;
		if (vd->regincl != NULL) {
			i = VRE_exec(vd->regincl, VSL_DATA(p), VSL_LEN(p),
			    0, 0, NULL, 0);
			if (i == VRE_ERROR_NOMATCH)
				continue;
		}
		if (vd->regexcl != NULL) {
			i = VRE_exec(vd->regincl, VSL_DATA(p), VSL_LEN(p),
			    0, 0, NULL, 0);
			if (i != VRE_ERROR_NOMATCH)
				continue;
		}
		*pp = p;
		return (1);
	}
}

/*--------------------------------------------------------------------*/

int
VSL_Dispatch(struct VSL_data *vd, vsl_handler *func, void *priv)
{
	int i;
	unsigned u, l, s;
	uint32_t *p;

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);
	while (1) {
		i = VSL_NextLog(vd, &p);
		if (i != 1)
			return (i);
		u = VSL_ID(p);
		l = VSL_LEN(p);
		s = 0;
		if (vbit_test(vd->vbm_backend, u))
			s |= VSL_S_BACKEND;
		if (vbit_test(vd->vbm_client, u))
			s |= VSL_S_CLIENT;
		if (func(priv, VSL_TAG(p), u, l, s, VSL_DATA(p)))
			return (1);
	}
}

/*--------------------------------------------------------------------*/

int
VSL_H_Print(void *priv, enum shmlogtag tag, unsigned fd, unsigned len,
    unsigned spec, const char *ptr)
{
	FILE *fo = priv;
	int type;

	assert(fo != NULL);

	type = (spec & VSL_S_CLIENT) ? 'c' :
	    (spec & VSL_S_BACKEND) ? 'b' : '-';

	if (tag == SLT_Debug) {
		fprintf(fo, "%5u %-12s %c \"", fd, VSL_tags[tag], type);
		while (len-- > 0) {
			if (*ptr >= ' ' && *ptr <= '~')
				fprintf(fo, "%c", *ptr);
			else
				fprintf(fo, "%%%02x", (unsigned char)*ptr);
			ptr++;
		}
		fprintf(fo, "\"\n");
		return (0);
	}
	fprintf(fo, "%5u %-12s %c %.*s\n", fd, VSL_tags[tag], type, len, ptr);
	return (0);
}

/*--------------------------------------------------------------------*/

int
VSL_OpenLog(struct VSL_data *vd)
{
	struct shmalloc *sha;

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);
	sha = vsl_find_alloc(vd, VSL_CLASS_LOG, "", "");
	assert(sha != NULL);

	vd->log_start = SHA_PTR(sha);
	vd->log_end = SHA_NEXT(sha);
	vd->log_ptr = vd->log_start + 1;

	vd->last_seq = vd->log_start[0];
	VRMB();
	if (!vd->d_opt && vd->r_fd == -1) {
		while (*vd->log_ptr != VSL_ENDMARKER)
			vd->log_ptr = VSL_NEXT(vd->log_ptr);
	}
	return (0);
}
