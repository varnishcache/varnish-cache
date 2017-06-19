/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 */

#include "config.h"

#include <sys/mman.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vdef.h"
#include "vas.h"
#include "miniobj.h"

#include "vin.h"
#include "vsb.h"
#include "vsm_priv.h"
#include "vsc_priv.h"
#include "vtim.h"

#include "vapi/vsm.h"


#ifndef MAP_HASSEMAPHORE
#define MAP_HASSEMAPHORE 0 /* XXX Linux */
#endif

const struct vsm_valid VSM_invalid[1] = {{"invalid"}};
const struct vsm_valid VSM_valid[1] = {{"valid"}};
const struct vsm_valid VSM_similar[1] = {{"similar"}};

/*--------------------------------------------------------------------*/

struct vsc;

struct vsm {
	unsigned		magic;
#define VSM_MAGIC		0x6e3bd69b

	struct vsb		*diag;

	char			*dname;
	char			*iname;

	struct stat		fstat;

	int			vsm_fd;
	struct VSM_head		*head;
	char			*b;
	char			*e;

	uint64_t		age_ok;
	double			t_ok;

	struct vsc		*vsc;
};

/*--------------------------------------------------------------------*/

struct vsm *
VSM_New(void)
{
	struct vsm *vd;

	ALLOC_OBJ(vd, VSM_MAGIC);
	if (vd == NULL)
		return (vd);

	vd->vsm_fd = -1;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	return (vd);
}

/*--------------------------------------------------------------------*/

void
VSM_SetVSC(struct vsm *vd, struct vsc *vsc)
{
	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	vd->vsc = vsc;
}

struct vsc *
VSM_GetVSC(const struct vsm *vd)
{
	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	return (vd->vsc);
}

/*--------------------------------------------------------------------*/

static int
vsm_diag(struct vsm *vd, const char *fmt, ...)
{
	va_list ap;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(fmt);

	if (vd->diag == NULL)
		vd->diag = VSB_new_auto();
	AN(vd->diag);
	VSB_clear(vd->diag);
	va_start(ap, fmt);
	VSB_vprintf(vd->diag, fmt, ap);
	va_end(ap);
	AZ(VSB_finish(vd->diag));
	return (-1);
}
/*--------------------------------------------------------------------*/

const char *
VSM_Error(const struct vsm *vd)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	if (vd->diag == NULL)
		return (NULL);
	else
		return (VSB_data(vd->diag));
}

/*--------------------------------------------------------------------*/

void
VSM_ResetError(struct vsm *vd)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	if (vd->diag == NULL)
		return;
	VSB_destroy(&vd->diag);
}

/*--------------------------------------------------------------------*/

int
VSM_n_Arg(struct vsm *vd, const char *arg)
{
	char *dname = NULL;
	struct vsb *vsb;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	if (vd->head)
		return (vsm_diag(vd, "VSM_n_Arg: Already open"));
	if (VIN_n_Arg(arg, &dname))
		return (vsm_diag(vd, "Invalid instance name: %s",
		    strerror(errno)));
	AN(dname);
	vsb = VSB_new_auto();
	AN(vsb);
	VSB_printf(vsb, "%s%s", dname, VSM_FILENAME);
	AZ(VSB_finish(vsb));

	REPLACE(vd->dname, dname);
	REPLACE(vd->iname, VSB_data(vsb));

	VSB_destroy(&vsb);
	free(dname);

	return (1);
}

/*--------------------------------------------------------------------*/

const char *
VSM_Name(const struct vsm *vd)
{
	struct vsm_fantom vt;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	if (VSM_Get(vd, &vt, "Arg", "-i"))
		return (vt.b);
	return ("");
}

/*--------------------------------------------------------------------*/

void
VSM_Delete(struct vsm *vd)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	VSM_Close(vd);
	if (vd->vsc != NULL)
		VSC_Delete(vd->vsc);
	VSM_ResetError(vd);
	free(vd->dname);
	FREE_OBJ(vd);
}

/*--------------------------------------------------------------------
 * The VSM open function
 *
 * Return:
 *	0 = success
 *	<0 = failure
 *
 */

/*--------------------------------------------------------------------*/

static void *ppp;

int
VSM_Open(struct vsm *vd)
{
	int i;
	struct VSM_head slh;
	void *v;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	if (vd->head != NULL)
		/* Already open */
		return (0);

	if (vd->dname == NULL) {
		/* Use default (hostname) */
		i = VSM_n_Arg(vd, "");
		if (i < 0)
			return (i);
		AN(vd->dname);
	}

	vd->vsm_fd = open(vd->iname, O_RDONLY);
	if (vd->vsm_fd < 0)
		return (vsm_diag(vd, "Cannot open %s: %s",
		    vd->iname, strerror(errno)));

	AZ(fstat(vd->vsm_fd, &vd->fstat));
	if (!S_ISREG(vd->fstat.st_mode)) {
		closefd(&vd->vsm_fd);
		return (vsm_diag(vd, "%s is not a regular file", vd->iname));
	}

	i = read(vd->vsm_fd, &slh, sizeof slh);
	if (i != sizeof slh) {
		closefd(&vd->vsm_fd);
		return (vsm_diag(vd, "Cannot read %s: %s",
		    vd->iname, strerror(errno)));
	}

	if (memcmp(slh.marker, VSM_HEAD_MARKER, sizeof slh.marker)) {
		closefd(&vd->vsm_fd);
		return (vsm_diag(vd, "Not a VSM file %s", vd->iname));
	}

	if (slh.alloc_seq == 0) {
		closefd(&vd->vsm_fd);
		return (vsm_diag(vd,
		    "Abandoned VSM file (Varnish not running?) %s",
		    vd->iname));
	}

	v = mmap(ppp, slh.shm_size,
	    PROT_READ, MAP_SHARED|MAP_HASSEMAPHORE, vd->vsm_fd, 0);
	if (v == MAP_FAILED)
		v = mmap(NULL, slh.shm_size,
		    PROT_READ, MAP_SHARED|MAP_HASSEMAPHORE, vd->vsm_fd, 0);
	if (v == MAP_FAILED) {
		closefd(&vd->vsm_fd);
		return (vsm_diag(vd, "Cannot mmap %s: %s",
		    vd->iname, strerror(errno)));
	}
	/*
	 * Force failure of client depends on remapping at same address.
	 */
	ppp = (char*)v + getpagesize() * 1000;

	vd->head = v;
	vd->b = v;
	vd->e = vd->b + slh.shm_size;
	vd->age_ok = vd->head->age;
	vd->t_ok = VTIM_mono();

	return (0);
}

/*--------------------------------------------------------------------*/

int
VSM_IsOpen(const struct vsm *vd)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	return (vd->head != NULL);
}

/*--------------------------------------------------------------------*/

void
VSM_Close(struct vsm *vd)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	if (vd->head == NULL)
		return;

	assert(vd->vsm_fd >= 0);
	AZ(munmap((void*)vd->b, vd->e - vd->b));
	vd->b = NULL;
	vd->e = NULL;
	vd->head = NULL;
	closefd(&vd->vsm_fd);
}

/*--------------------------------------------------------------------*/

int
VSM_Abandoned(struct vsm *vd)
{
	struct stat st;
	double now;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);

	if (vd->head == NULL)
		/* Not open */
		return (1);
	if (!vd->head->alloc_seq)
		/* Flag of abandonment set by mgt */
		return (1);
	if (vd->head->age < vd->age_ok)
		/* Age going backwards */
		return (1);
	now = VTIM_mono();
	if (vd->head->age == vd->age_ok && now - vd->t_ok > 2.) {
		/* No age change for 2 seconds, stat the file */
		if (stat(vd->iname, &st))
			return (1);
		if (st.st_dev != vd->fstat.st_dev)
			return (1);
		if (st.st_ino != vd->fstat.st_ino)
			return (1);
		vd->t_ok = now;
	} else if (vd->head->age > vd->age_ok) {
		/* It is aging, update timestamps */
		vd->t_ok = now;
		vd->age_ok = vd->head->age;
	}
	return (0);
}

/*--------------------------------------------------------------------*/

void
VSM__iter0(const struct vsm *vd, struct vsm_fantom *vf)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vf);

	memset(vf, 0, sizeof *vf);
}

int
VSM__itern(const struct vsm *vd, struct vsm_fantom *vf)
{
	struct VSM_chunk *c = NULL;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vf);

	if (!vd->head)
		return (0);	/* Not open */
	if (vd->head->alloc_seq == 0)
		return (0);	/* abandoned VSM */
	else if (vf->chunk != NULL) {
		/* get next chunk */
		if (vf->priv != vd->head->alloc_seq)
			return (0); /* changes during iteration */
		if (vf->chunk->len == 0)
			return (0); /* free'd during iteration */
		if (vf->chunk->next == 0)
			return (0); /* last */
		c = (struct VSM_chunk *)(void*)(vd->b + vf->chunk->next);
		assert(c != vf->chunk);
	} else if (vd->head->first == 0) {
		return (0);	/* empty vsm */
	} else {
		/* get first chunk */
		AZ(vf->chunk);
		c = (struct VSM_chunk *)(void*)(vd->b + vd->head->first);
	}
	AN(c);
	if (memcmp(c->marker, VSM_CHUNK_MARKER, sizeof c->marker))
		return (0);	/* XXX - assert? */

	vf->chunk = c;
	vf->priv = vd->head->alloc_seq;
	vf->b = (void*)(vf->chunk + 1);
	vf->e = (char*)vf->b + vf->chunk->len;

	AZ(vf->chunk->class[sizeof vf->chunk->class - 1]);
	REPLACE(vf->class, vf->chunk->class);

	AZ(vf->chunk->type[sizeof vf->chunk->type - 1]);
	REPLACE(vf->type, vf->chunk->type);

	AZ(vf->chunk->ident[sizeof vf->chunk->ident - 1]);
	REPLACE(vf->ident, vf->chunk->ident);

	return (1);
}

/*--------------------------------------------------------------------*/

int
VSM_Map(struct vsm *vd, struct vsm_fantom *vf)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vf);
	AN(vf->b);
	return (0);
}

/*--------------------------------------------------------------------*/

int
VSM_Unmap(struct vsm *vd, struct vsm_fantom *vf)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vf);
	AN(vf->b);
	return (0);
}

/*--------------------------------------------------------------------*/

const struct vsm_valid *
VSM_StillValid(const struct vsm *vd, struct vsm_fantom *vf)
{
	struct vsm_fantom f2;

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	AN(vf);
	if (!vd->head)
		return (VSM_invalid);
	if (!vd->head->alloc_seq)
		return (VSM_invalid);
	if (vf->chunk == NULL)
		return (VSM_invalid);
	if (vf->priv == vd->head->alloc_seq)
		return (VSM_valid);
	VSM_FOREACH(&f2, vd) {
		if (f2.chunk != vf->chunk || f2.b != vf->b || f2.e != vf->e)
			continue;
		if (strcmp(f2.class, vf->class))
			continue;
		if (strcmp(f2.type, vf->type))
			continue;
		if (strcmp(f2.ident, vf->ident))
			continue;
		vf->priv = vd->head->alloc_seq;
		return (VSM_similar);
	}
	return (VSM_invalid);
}

int
VSM_Get(const struct vsm *vd, struct vsm_fantom *vf,
    const char *class, const char *ident)
{

	CHECK_OBJ_NOTNULL(vd, VSM_MAGIC);
	VSM_FOREACH(vf, vd) {
		if (strcmp(vf->class, class))
			continue;
		if (ident != NULL && strcmp(vf->ident, ident))
			continue;
		return (1);
	}
	memset(vf, 0, sizeof *vf);
	return (0);
}
