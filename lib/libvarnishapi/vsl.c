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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vas.h"
#include "vsm.h"
#include "vsl.h"
#include "vre.h"
#include "vbm.h"
#include "miniobj.h"
#include "varnishapi.h"

#include "vsm_api.h"
#include "vsl_api.h"
#include "vmb.h"

/*--------------------------------------------------------------------*/

const char *VSL_tags[256] = {
#define SLTM(foo)       [SLT_##foo] = #foo,
#include "vsl_tags.h"
#undef SLTM
};

/*--------------------------------------------------------------------*/

void
VSL_Setup(struct VSM_data *vd)
{
	struct vsl *vsl;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AZ(vd->vsc);
	AZ(vd->vsl);
	ALLOC_OBJ(vsl, VSL_MAGIC);
	AN(vsl);

	vd->vsl = vsl;

	vsl->regflags = 0;

	/* XXX: Allocate only if log access */
	vsl->vbm_client = vbit_init(4096);
	vsl->vbm_backend = vbit_init(4096);
	vsl->vbm_supress = vbit_init(256);
	vsl->vbm_select = vbit_init(256);

	vsl->r_fd = -1;
	/* XXX: Allocate only if -r option given ? */
	vsl->rbuflen = 256;      /* XXX ?? */
	vsl->rbuf = malloc(vsl->rbuflen * 4L);
	assert(vsl->rbuf != NULL);

	vsl->num_matchers = 0;
	VTAILQ_INIT(&vsl->matchers);
}

/*--------------------------------------------------------------------*/

void
VSL_Delete(struct VSM_data *vd)
{
	struct vsl *vsl;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	vsl = vd->vsl;
	vd->vsl = NULL;
	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);

	vbit_destroy(vsl->vbm_client);
	vbit_destroy(vsl->vbm_backend);
	vbit_destroy(vsl->vbm_supress);
	vbit_destroy(vsl->vbm_select);
	free(vsl->rbuf);

	FREE_OBJ(vsl);
}

/*--------------------------------------------------------------------*/

void
VSL_Select(const struct VSM_data *vd, unsigned tag)
{
	struct vsl *vsl;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	vsl = vd->vsl;
	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	vbit_set(vsl->vbm_select, tag);
}


/*--------------------------------------------------------------------*/

void
VSL_NonBlocking(const struct VSM_data *vd, int nb)
{
	struct vsl *vsl;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	vsl = vd->vsl;
	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	if (nb)
		vsl->flags |= F_NON_BLOCKING;
	else
		vsl->flags &= ~F_NON_BLOCKING;
}

/*--------------------------------------------------------------------*/

static int
vsl_nextlog(struct vsl *vsl, uint32_t **pp)
{
	unsigned w, l;
	uint32_t t;
	int i;

	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);

	if (vsl->r_fd != -1) {
		assert(vsl->rbuflen >= 8);
		i = read(vsl->r_fd, vsl->rbuf, 8);
		if (i != 8)
			return (-1);
		l = 2 + VSL_WORDS(VSL_LEN(vsl->rbuf));
		if (vsl->rbuflen < l) {
			l += 256;
			vsl->rbuf = realloc(vsl->rbuf, l * 4L);
			assert(vsl->rbuf != NULL);
			vsl->rbuflen = l;
		}
		i = read(vsl->r_fd, vsl->rbuf + 2, l * 4L - 8L);
		if (i != l)
			return (-1);
		*pp = vsl->rbuf;
		return (1);
	}
	for (w = 0; w < TIMEOUT_USEC;) {
		t = *vsl->log_ptr;

		if (t == VSL_WRAPMARKER ||
		    (t == VSL_ENDMARKER && vsl->last_seq != vsl->log_start[0])) {
			vsl->log_ptr = vsl->log_start + 1;
			vsl->last_seq = vsl->log_start[0];
			VRMB();
			continue;
		}
		if (t == VSL_ENDMARKER) {
			if (vsl->flags & F_NON_BLOCKING)
				return (-1);
			w += SLEEP_USEC;
			AZ(usleep(SLEEP_USEC));
			continue;
		}
		*pp = (void*)(uintptr_t)vsl->log_ptr; /* Loose volatile */
		vsl->log_ptr = VSL_NEXT(vsl->log_ptr);
		return (1);
	}
	*pp = NULL;
	return (0);
}

int
VSL_NextLog(const struct VSM_data *vd, uint32_t **pp, uint64_t *mb)
{
	struct vsl *vsl;
	uint32_t *p;
	unsigned char t;
	unsigned u;
	int i;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	vsl = vd->vsl;
	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);

	while (1) {
		i = vsl_nextlog(vsl, &p);
		if (i != 1)
			return (i);
		u = VSL_ID(p);
		t = VSL_TAG(p);
		switch(t) {
		case SLT_SessionOpen:
		case SLT_ReqStart:
			vbit_set(vsl->vbm_client, u);
			vbit_clr(vsl->vbm_backend, u);
			break;
		case SLT_BackendOpen:
		case SLT_BackendXID:
			vbit_clr(vsl->vbm_client, u);
			vbit_set(vsl->vbm_backend, u);
			break;
		default:
			break;
		}
		if (vsl->skip) {
			--vsl->skip;
			continue;
		} else if (vsl->keep) {
			if (--vsl->keep == 0)
				return (-1);
		}

		if (vbit_test(vsl->vbm_select, t)) {
			*pp = p;
			return (1);
		}
		if (vbit_test(vsl->vbm_supress, t))
			continue;
		if (vsl->b_opt && !vbit_test(vsl->vbm_backend, u))
			continue;
		if (vsl->c_opt && !vbit_test(vsl->vbm_client, u))
			continue;
		if (vsl->regincl != NULL) {
			i = VRE_exec(vsl->regincl, VSL_DATA(p), VSL_LEN(p),
			    0, 0, NULL, 0);
			if (i == VRE_ERROR_NOMATCH)
				continue;
		}
		if (vsl->regexcl != NULL) {
			i = VRE_exec(vsl->regexcl, VSL_DATA(p), VSL_LEN(p),
			    0, 0, NULL, 0);
			if (i != VRE_ERROR_NOMATCH)
				continue;
		}
		if (mb != NULL) {
			struct vsl_re_match *vrm;
			int j = 0;
			VTAILQ_FOREACH(vrm, &vsl->matchers, next) {
				if (vrm->tag == t) {
					i = VRE_exec(vrm->re, VSL_DATA(p),
						     VSL_LEN(p), 0, 0, NULL, 0);
					if (i >= 0)
						*mb |= 1 << j;
				}
				j++;
			}
		}
		*pp = p;
		return (1);
	}
}

/*--------------------------------------------------------------------*/

int
VSL_Dispatch(struct VSM_data *vd, VSL_handler_f *func, void *priv)
{
	struct vsl *vsl;
	int i;
	unsigned u, l, s;
	uint32_t *p;
	uint64_t bitmap;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	vsl = vd->vsl;
	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);

	while (1) {
		bitmap = 0;
		i = VSL_NextLog(vd, &p, &bitmap);
		if (i == 0 && VSM_ReOpen(vd, 0) == 1)
			continue;
		if (i != 1)
			return (i);
		u = VSL_ID(p);
		l = VSL_LEN(p);
		s = 0;
		if (vbit_test(vsl->vbm_backend, u))
			s |= VSL_S_BACKEND;
		if (vbit_test(vsl->vbm_client, u))
			s |= VSL_S_CLIENT;
		if (func(priv, VSL_TAG(p), u, l, s, VSL_DATA(p), bitmap))
			return (1);
	}
}

/*--------------------------------------------------------------------*/

int
VSL_H_Print(void *priv, enum VSL_tag_e tag, unsigned fd, unsigned len,
    unsigned spec, const char *ptr, uint64_t bitmap)
{
	FILE *fo = priv;
	int type;

	(void) bitmap;
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

void
VSL_Open_CallBack(struct VSM_data *vd)
{
	struct vsl *vsl;
	struct VSM_chunk *sha;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	vsl = vd->vsl;
	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);
	sha = VSM_find_alloc(vd, VSL_CLASS, "", "");
	assert(sha != NULL);

	vsl->log_start = VSM_PTR(sha);
	vsl->log_end = VSM_NEXT(sha);
	vsl->log_ptr = vsl->log_start + 1;

	vsl->last_seq = vsl->log_start[0];
	VRMB();
}

/*--------------------------------------------------------------------*/

int
VSL_Open(struct VSM_data *vd, int diag)
{
	struct vsl *vsl;
	int i;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	vsl = vd->vsl;
	CHECK_OBJ_NOTNULL(vsl, VSL_MAGIC);

	i = VSM_Open(vd, diag);
	if (i)
		return (i);

	if (!vsl->d_opt && vsl->r_fd == -1) {
		while (*vsl->log_ptr != VSL_ENDMARKER)
			vsl->log_ptr = VSL_NEXT(vsl->log_ptr);
	}
	return (0);
}

/*--------------------------------------------------------------------*/

int VSL_Matched(const struct VSM_data *vd, uint64_t bitmap)
{
	if (vd->vsl->num_matchers > 0) {
		uint64_t t;
		t = vd->vsl->num_matchers | (vd->vsl->num_matchers - 1);
		return (bitmap == t);
	}
	return (1);
}
