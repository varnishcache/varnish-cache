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
#include <sys/mman.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
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

#include "vsl.h"

#ifndef MAP_HASSEMAPHORE
#define MAP_HASSEMAPHORE 0 /* XXX Linux */
#endif

/*--------------------------------------------------------------------*/

struct VSL_data *
VSL_New(void)
{
	struct VSL_data *vd;

	vd = calloc(sizeof *vd, 1);
	assert(vd != NULL);
	vd->regflags = 0;
	vd->magic = VSL_MAGIC;
	vd->vsl_fd = -1;

	/* XXX: Allocate only if log access */
	vd->vbm_client = vbit_init(4096);
	vd->vbm_backend = vbit_init(4096);
	vd->vbm_supress = vbit_init(256);
	vd->vbm_select = vbit_init(256);

	vd->r_fd = -1;
	/* XXX: Allocate only if -r option given ? */
	vd->rbuflen = SHMLOG_NEXTTAG + 256;
	vd->rbuf = malloc(vd->rbuflen);
	assert(vd->rbuf != NULL);

	VTAILQ_INIT(&vd->sf_list);

	return (vd);
}

/*--------------------------------------------------------------------*/

void
VSL_Delete(struct VSL_data *vd)
{
	struct vsl_sf *sf;

	VSL_Close(vd);
	vbit_destroy(vd->vbm_client);
	vbit_destroy(vd->vbm_backend);
	vbit_destroy(vd->vbm_supress);
	vbit_destroy(vd->vbm_select);
	free(vd->n_opt);
	free(vd->rbuf);
	free(vd->fname);

	while(!VTAILQ_EMPTY(&vd->sf_list)) {
		sf = VTAILQ_FIRST(&vd->sf_list);	
		VTAILQ_REMOVE(&vd->sf_list, sf, next);
		free(sf->class);
		free(sf->ident);
		free(sf->name);
		free(sf);
	}

	free(vd);
}

/*--------------------------------------------------------------------*/

static int
vsl_open(struct VSL_data *vd, int rep)
{
	int i;
	struct shmloghead slh;

	if (vd->vsl_lh != NULL)
		return (0);

	vd->vsl_fd = open(vd->fname, O_RDONLY);
	if (vd->vsl_fd < 0) {
		if (rep)
			fprintf(stderr, "Cannot open %s: %s\n",
			    vd->fname, strerror(errno));
		return (1);
	}

	assert(fstat(vd->vsl_fd, &vd->fstat) == 0);
	if (!S_ISREG(vd->fstat.st_mode)) {
		if (rep)
			fprintf(stderr, "%s is not a regular file\n",
			    vd->fname);
		return (1);
	}

	i = read(vd->vsl_fd, &slh, sizeof slh);
	if (i != sizeof slh) {
		if (rep)
			fprintf(stderr, "Cannot read %s: %s\n",
			    vd->fname, strerror(errno));
		return (1);
	}
	if (slh.magic != SHMLOGHEAD_MAGIC) {
		if (rep)
			fprintf(stderr, "Wrong magic number in file %s\n",
			    vd->fname);
		return (1);
	}

	vd->vsl_lh = (void *)mmap(NULL, slh.shm_size,
	    PROT_READ, MAP_SHARED|MAP_HASSEMAPHORE, vd->vsl_fd, 0);
	if (vd->vsl_lh == MAP_FAILED) {
		if (rep)
			fprintf(stderr, "Cannot mmap %s: %s\n",
			    vd->fname, strerror(errno));
		return (1);
	}
	vd->vsl_end = (uint8_t *)vd->vsl_lh + slh.shm_size;

	while(slh.alloc_seq == 0)
		usleep(50000);
	vd->alloc_seq = slh.alloc_seq;
	return (0);
}

int
VSL_Open(struct VSL_data *vd)
{
	
	return (vsl_open(vd, 1));
}

/*--------------------------------------------------------------------*/

void
VSL_Close(struct VSL_data *vd)
{
	if (vd->vsl_lh == NULL)
		return;
	assert(0 == munmap((void*)vd->vsl_lh, vd->vsl_lh->shm_size));
	vd->vsl_lh = NULL;
	assert(vd->vsl_fd >= 0);
	assert(0 == close(vd->vsl_fd));
	vd->vsl_fd = -1;
}

/*--------------------------------------------------------------------*/

int
VSL_ReOpen(struct VSL_data *vd)
{
	struct stat st;
	int i;

	if (vd->vsl_lh == NULL)
		return (-1);

	if (stat(vd->fname, &st))
		return (0);

	if (st.st_dev == vd->fstat.st_dev && st.st_ino == vd->fstat.st_ino)
		return (0);

	VSL_Close(vd);
	for (i = 0; i < 5; i++) {
		if (!vsl_open(vd, 0))
			return (1);
	}
	if (vsl_open(vd, 1))
		return (-1);
	return (1);
}

/*--------------------------------------------------------------------*/

struct shmalloc *
vsl_iter0(const struct VSL_data *vd)
{

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);
	if (vd->alloc_seq != vd->vsl_lh->alloc_seq)
		return(NULL);
	CHECK_OBJ_NOTNULL(&vd->vsl_lh->head, SHMALLOC_MAGIC);
	return (&vd->vsl_lh->head);
}

void
vsl_itern(const struct VSL_data *vd, struct shmalloc **pp)
{

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);
	if (vd->alloc_seq != vd->vsl_lh->alloc_seq) {
		*pp = NULL;
		return;
	}
	CHECK_OBJ_NOTNULL(*pp, SHMALLOC_MAGIC);
	*pp = SHA_NEXT(*pp);
	if ((void*)(*pp) >= vd->vsl_end) {
		*pp = NULL;
		return;
	}
	CHECK_OBJ_NOTNULL(*pp, SHMALLOC_MAGIC);
}

/*--------------------------------------------------------------------*/

struct shmalloc *
vsl_find_alloc(const struct VSL_data *vd, const char *class, const char *type, const char *ident)
{
	struct shmalloc *sha;

	assert (vd->vsl_lh != NULL);
	VSL_FOREACH(sha, vd) {
		CHECK_OBJ_NOTNULL(sha, SHMALLOC_MAGIC);
		if (strcmp(sha->class, class)) 
			continue;
		if (type != NULL && strcmp(sha->type, type))
			continue;
		if (ident != NULL && strcmp(sha->ident, ident))
			continue;
		return (sha);
	}
	return (NULL);
}

/*--------------------------------------------------------------------*/

void *
VSL_Find_Alloc(struct VSL_data *vd, const char *class, const char *type, const char *ident,
    unsigned *lenp)
{
	struct shmalloc *sha;

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);
	if (VSL_Open(vd))
		return (NULL);
	sha = vsl_find_alloc(vd, class, type, ident);
	if (sha == NULL)
		return (NULL);
	if (lenp != NULL)
		*lenp = sha->len - sizeof *sha;
	return (SHA_PTR(sha));
}

/*--------------------------------------------------------------------*/

int
VSL_OpenLog(struct VSL_data *vd)
{
	unsigned char *p;
	struct shmalloc *sha;

	CHECK_OBJ_NOTNULL(vd, VSL_MAGIC);
	if (VSL_Open(vd))
		return (-1);
	sha = vsl_find_alloc(vd, VSL_CLASS_LOG, VSL_TYPE_STAT_SMA, "");
	assert(sha != NULL);

	vd->log_start = SHA_PTR(sha);
	vd->log_end = vd->log_start + sha->len - sizeof *sha;
	vd->log_ptr = vd->log_start + 1;

	if (!vd->d_opt && vd->r_fd == -1) {
		for (p = vd->log_ptr; *p != SLT_ENDMARKER; )
			p += SHMLOG_LEN(p) + SHMLOG_NEXTTAG;
		vd->log_ptr = p;
	}
	return (0);
}

/*--------------------------------------------------------------------*/

const char *
VSL_Name(const struct VSL_data *vd)
{

	return (vd->n_opt);
}
