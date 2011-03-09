/*-
 * Copyright (c) 2010 Linpro AS
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

#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "vrt.h"
#include "../../bin/varnishd/cache.h"

#include "vcc_if.h"

VSLIST_HEAD(cached_file_list, cached_file);

struct cached_file {
	unsigned	magic;
#define CACHED_FILE_MAGIC 0xa8e9d87a
	char		*file_name;
	char		*contents;
	time_t		last_modification;
	off_t		file_sz;
	VSLIST_ENTRY(cached_file) next;
};

static void
free_cached_files(void *file_list)
{
	struct cached_file *iter, *tmp;
	struct cached_file_list *list = file_list;
	VSLIST_FOREACH_SAFE(iter, list, next, tmp) {
		CHECK_OBJ(iter, CACHED_FILE_MAGIC);
		free(iter->file_name);
		free(iter->contents);
		FREE_OBJ(iter);
	}
	free(file_list);
}

static pthread_rwlock_t filelist_lock = PTHREAD_RWLOCK_INITIALIZER;
static int filelist_update = 0;

const char *
vmod_fileread(struct sess *sp, struct vmod_priv *priv, const char *file_name)
{
	struct cached_file *iter = NULL;
	struct stat buf;
	struct cached_file_list *list;
	int fd, my_filelist_update;

	(void)sp;

	AZ(pthread_rwlock_rdlock(&filelist_lock));

	if (priv->free == NULL) {
		AZ(pthread_rwlock_unlock(&filelist_lock));
		/*
		 * Another thread may already have initialized priv
		 * here, making the repeat check necessary.
		 */
		AZ(pthread_rwlock_wrlock(&filelist_lock));
		if (priv->free == NULL) {
			priv->free = free_cached_files;
			priv->priv = malloc(sizeof(struct cached_file_list));
			AN(priv->priv);
			list = priv->priv;
			VSLIST_INIT(list);
		}
		AZ(pthread_rwlock_unlock(&filelist_lock));
		AZ(pthread_rwlock_rdlock(&filelist_lock));
	} else {
		list = priv->priv;
		VSLIST_FOREACH(iter, list, next) {
			CHECK_OBJ(iter, CACHED_FILE_MAGIC);
			if (strcmp(iter->file_name, file_name) == 0) {
				/* This thread was holding a read lock. */
				AZ(pthread_rwlock_unlock(&filelist_lock));
				return iter->contents;
			}
		}
	}

	my_filelist_update = filelist_update;

	/* This thread was holding a read lock. */
	AZ(pthread_rwlock_unlock(&filelist_lock));

	if ((fd = open(file_name, O_RDONLY)) == -1)
		return "";

	AZ(fstat(fd, &buf)i);

	AZ(pthread_rwlock_wrlock(&filelist_lock));

	if (my_filelist_update != filelist_update) {

		/*
		 * Small optimization: search through the linked list again
		 * only if something has been changed.
		 */
		VSLIST_FOREACH(iter, list, next) {
			CHECK_OBJ(iter, CACHED_FILE_MAGIC);
			if (strcmp(iter->file_name, file_name) == 0) {
				/* This thread was holding a write lock. */
				AZ(pthread_rwlock_unlock(&filelist_lock));
				return iter->contents;
			}
		}
	}

	ALLOC_OBJ(iter, CACHED_FILE_MAGIC);
	AN(iter);

	iter->file_name = strdup(file_name);
	iter->last_modification = buf.st_mtime;

	iter->contents = malloc(buf.st_size + 1);
	AN(iter->contents);
	iter->file_sz = read(fd, iter->contents, buf.st_size);
	assert(iter->file_sz == buf.st_size);
	AZ(close(fd));

	iter->contents[iter->file_sz] = '\0';

	VSLIST_INSERT_HEAD(list, iter, next);

	filelist_update++;

	/* This thread was holding a write lock. */
	AZ(pthread_rwlock_unlock(&filelist_lock));
	return iter->contents;
}
