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

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "miniobj.h"
#include "vas.h"

#include "vapi/vsm.h"
#include "vapi/vsm_int.h"
#include "vbm.h"
#include "vin.h"
#include "vsm_api.h"

#ifndef MAP_HASSEMAPHORE
#define MAP_HASSEMAPHORE 0 /* XXX Linux */
#endif

/*--------------------------------------------------------------------*/

struct VSM_data *
VSM_New(void)
{
	struct VSM_data *vd;

	ALLOC_OBJ(vd, VSM_MAGIC);
	if (vd == NULL)
		return (vd);

	vd->diag = (VSM_diag_f*)fprintf;
	vd->priv = stderr;

	vd->vsm_fd = -1;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	return (vd);
}

/*--------------------------------------------------------------------*/

void
VSM_Diag(struct VSM_data *vd, VSM_diag_f *func, void *priv)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	if (func == NULL)
		vd->diag = (VSM_diag_f*)getpid;
	else
		vd->diag = func;
	vd->priv = priv;
}

/*--------------------------------------------------------------------*/

int
VSM_n_Arg(struct VSM_data *vd, const char *opt)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	REPLACE(vd->n_opt, opt);
	AN(vd->n_opt);
	if (VIN_N_Arg(vd->n_opt, NULL, NULL, &vd->fname)) {
		vd->diag(vd->priv, "Invalid instance name: %s\n",
		    strerror(errno));
		return (-1);
	}
	return (1);
}

/*--------------------------------------------------------------------*/

const char *
VSM_Name(const struct VSM_data *vd)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	return (vd->n_opt);
}

/*--------------------------------------------------------------------*/

void
VSM_Delete(struct VSM_data *vd)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	VSM_Close(vd);

	free(vd->n_opt);
	free(vd->fname);

	if (vd->vsc != NULL)
		VSC_Delete(vd);
	if (vd->vsl != NULL)
		VSL_Delete(vd);

	FREE_OBJ(vd);
}

/*--------------------------------------------------------------------
 * The internal VSM open function
 *
 * Return:
 *	0 = sucess
 *	<0 = failure
 *
 */

static int
vsm_open(struct VSM_data *vd, int diag)
{
	int i;
	struct VSM_head slh;
	void *v;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AZ(vd->head);
	AN(vd->fname);

	vd->vsm_fd = open(vd->fname, O_RDONLY);
	if (vd->vsm_fd < 0) {
		if (diag)
			vd->diag(vd->priv, "Cannot open %s: %s\n",
			    vd->fname, strerror(errno));
		return (-1);
	}

	AZ(fstat(vd->vsm_fd, &vd->fstat));
	if (!S_ISREG(vd->fstat.st_mode)) {
		if (diag)
			vd->diag(vd->priv, "%s is not a regular file\n",
			    vd->fname);
		AZ(close(vd->vsm_fd));
		vd->vsm_fd = -1;
		return (-1);
	}

	i = read(vd->vsm_fd, &slh, sizeof slh);
	if (i != sizeof slh) {
		if (diag)
			vd->diag(vd->priv, "Cannot read %s: %s\n",
			    vd->fname, strerror(errno));
		AZ(close(vd->vsm_fd));
		vd->vsm_fd = -1;
		return (-1);
	}

	if (memcmp(slh.marker, VSM_HEAD_MARKER, sizeof slh.marker) ||
	    slh.alloc_seq == 0) {
		if (diag)
			vd->diag(vd->priv, "Not a VSM file %s\n",
			    vd->fname);
		AZ(close(vd->vsm_fd));
		vd->vsm_fd = -1;
		return (-1);
	}

	v = mmap(NULL, slh.shm_size,
	    PROT_READ, MAP_SHARED|MAP_HASSEMAPHORE, vd->vsm_fd, 0);
	if (v == MAP_FAILED) {
		if (diag)
			vd->diag(vd->priv, "Cannot mmap %s: %s\n",
			    vd->fname, strerror(errno));
		AZ(close(vd->vsm_fd));
		vd->vsm_fd = -1;
		return (-1);
	}
	vd->head = v;
	vd->b = v;
	vd->e = vd->b + slh.shm_size;

	return (0);
}

/*--------------------------------------------------------------------*/

int
VSM_Open(struct VSM_data *vd, int diag)

{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AZ(vd->head);
	if (!vd->n_opt)
		(void)VSM_n_Arg(vd, "");
	return (vsm_open(vd, diag));
}

/*--------------------------------------------------------------------*/

void
VSM_Close(struct VSM_data *vd)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	if (vd->head == NULL)
		return;

	assert(vd->vsm_fd >= 0);
	AZ(munmap((void*)vd->b, vd->e - vd->b));
	vd->b = NULL;
	vd->e = NULL;
	vd->head = NULL;
	AZ(close(vd->vsm_fd));
	vd->vsm_fd = -1;
}

/*--------------------------------------------------------------------*/

int
VSM_ReOpen(struct VSM_data *vd, int diag)
{
	struct stat st;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vd->head);

	if (vd->head->alloc_seq &&
	    !stat(vd->fname, &st) &&
	    st.st_dev == vd->fstat.st_dev &&
	    st.st_ino == vd->fstat.st_ino)
		return (0);

	VSM_Close(vd);
	return (vsm_open(vd, diag));
}

/*--------------------------------------------------------------------*/

unsigned
VSM_Seq(const struct VSM_data *vd)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	return (vd->head->alloc_seq);
}

/*--------------------------------------------------------------------*/

struct VSM_head *
VSM_Head(const struct VSM_data *vd)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vd->head);
	return(vd->head);
}

/*--------------------------------------------------------------------*/

void
VSM__iter0(struct VSM_data *vd, struct VSM_fantom *vf)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	memset(vf, 0, sizeof *vf);
}

int
VSM__itern(struct VSM_data *vd, struct VSM_fantom *vf)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	if (vf->priv != 0) {
		if (vf->priv != vd->head->alloc_seq)
			return (0);
		if (vf->chunk->len == 0)
			return (0);
		if (vf->chunk->next == 0)
			return (0);
		vf->chunk = (void*)(vd->b + vf->chunk->next);
	} else if (vd->head->first == 0) {
		return (0);
	} else {
		vf->chunk = (void*)(vd->b + vd->head->first);
	}
	if (memcmp(vf->chunk->marker, VSM_CHUNK_MARKER,
	    sizeof vf->chunk->marker))
		return (0);
	vf->priv = vd->head->alloc_seq;
	vf->b = (void*)(vf->chunk + 1);
	vf->e = (char*)vf->b + vf->chunk->len;
	if (vf->b == vf->e)
		return (0);
	return (1);
}

/*--------------------------------------------------------------------*/

int
VSM_StillValid(struct VSM_data *vd, struct VSM_fantom *vf)
{
	struct VSM_fantom f2;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	if (vf->priv == vd->head->alloc_seq)
		return (1);
	VSM_FOREACH_SAFE(&f2, vd) {
		if (f2.chunk == vf->chunk &&
		   f2.b == vf->b &&
		   f2.e == vf->e) {
			vf->priv = vd->head->alloc_seq;
			return (2);
		}
	}
	return (0);
}

int
VSM_Get(struct VSM_data *vd, struct VSM_fantom *vf, const char *class,
    const char *type, const char *ident)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	VSM_FOREACH_SAFE(vf, vd) {
		if (strcmp(vf->chunk->class, class))
			continue;
		if (type != NULL && strcmp(vf->chunk->type, type))
			continue;
		if (ident != NULL && strcmp(vf->chunk->ident, ident))
			continue;
		return (1);
	}
	memset(vf, 0, sizeof *vf);
	return (0);
}

/*--------------------------------------------------------------------*/

void *
VSM_Find_Chunk(struct VSM_data *vd, const char *class, const char *type,
    const char *ident, unsigned *lenp)
{
	struct VSM_fantom vf;

	if (VSM_Get(vd, &vf, class, type, ident)) {
		if (lenp != NULL)
			*lenp = (char*)vf.e - (char*)vf.b;
		return (vf.chunk);
	}
	if (lenp != NULL)
		*lenp = 0;
	return (NULL);
}

/*--------------------------------------------------------------------*/

struct VSM_chunk *
VSM_iter0(struct VSM_data *vd)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	VSM__iter0(vd, &vd->compat_vf);
	if (VSM__itern(vd, &vd->compat_vf))
		return(vd->compat_vf.chunk);
	return (NULL);
}

void
VSM_itern(struct VSM_data *vd, struct VSM_chunk **pp)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	if (VSM__itern(vd, &vd->compat_vf))
		*pp = vd->compat_vf.chunk;
	else
		*pp = NULL;
}
