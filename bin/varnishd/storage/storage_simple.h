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
 *
 * SML is a set of methods for simple stevedores which just do simple
 * memory allocation and leave all the high-level stuff to SML.
 *
 */

/* Object ------------------------------------------------------------*/

VTAILQ_HEAD(storagehead, storage);

struct object {
	unsigned		magic;
#define OBJECT_MAGIC		0x32851d42
	struct storage		*objstore;

	/* Fixed size attributes */
#define OBJ_FIXATTR(U, l, s)			\
	uint8_t			fa_##l[s];
#include "tbl/obj_attr.h"

	/* Variable size attributes */
#define OBJ_VARATTR(U, l)			\
	uint8_t			*va_##l;
#include "tbl/obj_attr.h"

#define OBJ_VARATTR(U, l)			\
	unsigned		va_##l##_len;
#include "tbl/obj_attr.h"

	/* Auxiliary attributes */
#define OBJ_AUXATTR(U, l)			\
	struct storage		*aa_##l;
#include "tbl/obj_attr.h"

	struct storagehead	list;
};

extern const struct obj_methods SML_methods;

struct object *SML_MkObject(const struct stevedore *, struct objcore *,
    void *ptr);

storage_allocobj_f SML_allocobj;
storage_panic_f SML_panic;
