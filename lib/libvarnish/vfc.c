/*-
 * Copyright (c) 2010-2020 Varnish Software AS
 * All rights reserved.
 *
 * Author: Sanjoy Das <sanjoy@playingwithpointers.com>
 * Author: Dridi Boukelmoune <dridi.boukelmoune@gmail.com>
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
 * XXX: It might make sense to use just a single global list of all files
 * XXX: and use the call-private pointer to point to the file instance on
 * XXX: that list.
 * XXX: Duplicates in the global list can be avoided by examining the
 * XXX: dev+inode fields of the stat structure.
 * XXX: Individual files would need to be refcounted, so they can be
 * XXX: deleted when no VCL's reference them.
 *
 * XXX: We should periodically stat(2) the filename and check if the
 * XXX: underlying file has been updated.
 */

#include "config.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vdef.h"
#include "vas.h"
#include "vfil.h"
#include "vqueue.h"
#include "miniobj.h"

#include "vrt.h" /* XXX: maybe VFC belongs in cache_varnishd.h */
#include "vfc.h"

static VTAILQ_HEAD(, vfc)	vfc_head = VTAILQ_HEAD_INITIALIZER(vfc_head);
static pthread_mutex_t		vfc_mtx = PTHREAD_MUTEX_INITIALIZER;

static void
vfc_unref_locked(struct vfc **vfcp)
{
	struct vfc *vfc;

	AN(vfcp);
	vfc = *vfcp;
	if (vfc == NULL)
		return;
	CHECK_OBJ(vfc, CACHED_FILE_MAGIC);
	assert(vfc->refcount > 0);
	if (--vfc->refcount > 0)
		*vfcp = NULL;
	else
		VTAILQ_REMOVE(&vfc_head, vfc, list);
}

static void
vfc_destroy(struct vfc **vfcp)
{
	struct vfc *vfc;

	TAKE_OBJ_NOTNULL(vfc, vfcp, CACHED_FILE_MAGIC);
	free(TRUST_ME(vfc->contents->blob));
	free(vfc->file_name);
	FREE_OBJ(vfc);
}

void
VFC_destroy(struct vfc **vfcp)
{

	AN(vfcp);
	AN(*vfcp);
	AZ(pthread_mutex_lock(&vfc_mtx));
	vfc_unref_locked(vfcp);
	AZ(pthread_mutex_unlock(&vfc_mtx));
	if (*vfcp != NULL)
		vfc_destroy(vfcp);
}

struct vfc *
VFC_find(struct vfc *old, const char *file_name)
{
	struct vfc *vfc;
	char *s;
	ssize_t sz;

	CHECK_OBJ_ORNULL(old, CACHED_FILE_MAGIC);

	if (file_name == NULL)
		return (NULL);

	if (old != NULL && !strcmp(file_name, old->file_name))
		return (old);

	AZ(pthread_mutex_lock(&vfc_mtx));
	vfc_unref_locked(&old);
	VTAILQ_FOREACH(vfc, &vfc_head, list) {
		if (!strcmp(file_name, vfc->file_name)) {
			vfc->refcount++;
			break;
		}
	}
	AZ(pthread_mutex_unlock(&vfc_mtx));
	if (old != NULL)
		vfc_destroy(&old);
	if (vfc != NULL)
		return (vfc);

	s = VFIL_readfile(NULL, file_name, &sz);
	if (s != NULL) {
		assert(sz > 0);
		ALLOC_OBJ(vfc, CACHED_FILE_MAGIC);
		AN(vfc);
		REPLACE(vfc->file_name, file_name);
		vfc->refcount = 1;
		vfc->contents->blob = s;
		vfc->contents->len = (size_t)sz;
		AZ(pthread_mutex_lock(&vfc_mtx));
		VTAILQ_INSERT_HEAD(&vfc_head, vfc, list);
		AZ(pthread_mutex_unlock(&vfc_mtx));
	}
	return (vfc);
}
