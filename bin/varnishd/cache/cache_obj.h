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
 */

/* Methods on objcore ------------------------------------------------*/

typedef void objupdatemeta_f(struct worker *, struct objcore *);
typedef void objfree_f(struct worker *, struct objcore *);
typedef struct lru *objgetlru_f(const struct objcore *);

/* This method is only used by SML (...to get to persistent) */
typedef struct object *sml_getobj_f(struct worker *, struct objcore *);

typedef int objiterator_f(struct worker *, struct objcore *,
    void *priv, objiterate_f *func);
typedef int objgetspace_f(struct worker *, struct objcore *,
     ssize_t *sz, uint8_t **ptr);
typedef void objextend_f(struct worker *, struct objcore *, ssize_t l);
typedef void objtrimstore_f(struct worker *, struct objcore *);
typedef void objslim_f(struct worker *, struct objcore *);
typedef void *objgetattr_f(struct worker *, struct objcore *,
    enum obj_attr attr, ssize_t *len);
typedef void *objsetattr_f(struct worker *, struct objcore *,
    enum obj_attr attr, ssize_t len, const void *ptr);
typedef uint64_t objgetlen_f(struct worker *, struct objcore *);

struct obj_methods {
	objfree_f	*objfree;
	objgetlru_f	*objgetlru;
	objupdatemeta_f	*objupdatemeta;

	sml_getobj_f	*sml_getobj;

	objiterator_f	*objiterator;
	objgetspace_f	*objgetspace;
	objextend_f	*objextend;
	objgetlen_f	*objgetlen;
	objtrimstore_f	*objtrimstore;
	objslim_f	*objslim;
	objgetattr_f	*objgetattr;
	objsetattr_f	*objsetattr;
};
