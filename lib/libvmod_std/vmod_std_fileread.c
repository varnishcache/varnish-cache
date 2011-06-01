/*-
 * Copyright (c) 2010-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Sanjoy Das <sanjoy@playingwithpointers.com>
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

#include <stdlib.h>
#include "vrt.h"
#include "../../bin/varnishd/cache.h"

#include "vcc_if.h"

struct frfile {
	unsigned			magic;
#define CACHED_FILE_MAGIC 0xa8e9d87a
	char				*file_name;
	char				*contents;
	int				refcount;
	VTAILQ_ENTRY(frfile)		list;
};

static VTAILQ_HEAD(, frfile)		frlist = VTAILQ_HEAD_INITIALIZER(frlist);
static pthread_mutex_t			frmtx = PTHREAD_MUTEX_INITIALIZER;

static void
free_frfile(void *ptr)
{
	struct frfile *frf;

	CAST_OBJ_NOTNULL(frf, ptr, CACHED_FILE_MAGIC);

	AZ(pthread_mutex_lock(&frmtx));
	if (--frf->refcount > 0)
		frf = NULL;
	else
		VTAILQ_REMOVE(&frlist, frf, list);
	AZ(pthread_mutex_unlock(&frmtx));
	if (frf != NULL) {
		free(frf->contents);
		free(frf->file_name);
		FREE_OBJ(frf);
	}
}

const char *
vmod_fileread(struct sess *sp, struct vmod_priv *priv, const char *file_name)
{
	struct frfile *frf;
	char *s;

	(void)sp;
	AN(priv);
	if (priv->priv != NULL) {
		CAST_OBJ_NOTNULL(frf, priv->priv, CACHED_FILE_MAGIC);
		return (frf->contents);
	}

	AZ(pthread_mutex_lock(&frmtx));
	VTAILQ_FOREACH(frf, &frlist, list) {
		if (!strcmp(file_name, frf->file_name)) {
			frf->refcount++;
			break;
		}
	}
	AZ(pthread_mutex_unlock(&frmtx));
	if (frf != NULL) {
		priv->free = free_frfile;
		priv->priv = frf;
		return (frf->contents);
	}

	s = vreadfile(NULL, file_name, NULL);
	if (s != NULL) {
		ALLOC_OBJ(frf, CACHED_FILE_MAGIC);
		AN(frf);
		frf->file_name = strdup(file_name);
		AN(frf->file_name);
		frf->refcount = 1;
		frf->contents = s;
		priv->free = free_frfile;
		priv->priv = frf;
		AZ(pthread_mutex_lock(&frmtx));
		VTAILQ_INSERT_HEAD(&frlist, frf, list);
		AZ(pthread_mutex_unlock(&frmtx));
	}
	return (s);
}
