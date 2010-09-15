/*-
 * Copyright (c) 2008-2009 Linpro AS
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
 * Storage method for synthetic content, based on vsb.
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "cache.h"
#include "vsb.h"
#include "stevedore.h"
#include "hash_slinger.h"

static struct lock		sms_mtx;

static void
sms_free(struct storage *sto)
{

	CHECK_OBJ_NOTNULL(sto, STORAGE_MAGIC);
	Lck_Lock(&sms_mtx);
	VSC_main->sms_nobj--;
	VSC_main->sms_nbytes -= sto->len;
	VSC_main->sms_bfree += sto->len;
	Lck_Unlock(&sms_mtx);
	vsb_delete(sto->priv);
	free(sto);
}

void
SMS_Init(void)
{

	Lck_New(&sms_mtx);
}

static struct stevedore sms_stevedore = {
	.magic	=	STEVEDORE_MAGIC,
	.name	=	"synth",
	.free	=	sms_free,
};

struct vsb *
SMS_Makesynth(struct object *obj)
{
	struct storage *sto;
	struct vsb *vsb;

	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);
	HSH_Freestore(obj);
	obj->len = 0;

	Lck_Lock(&sms_mtx);
	VSC_main->sms_nreq++;
	VSC_main->sms_nobj++;
	Lck_Unlock(&sms_mtx);

	sto = calloc(sizeof *sto, 1);
	XXXAN(sto);
	vsb = vsb_newauto();
	XXXAN(vsb);
	sto->priv = vsb;
	sto->len = 0;
	sto->space = 0;
	sto->fd = -1;
	sto->stevedore = &sms_stevedore;
	sto->magic = STORAGE_MAGIC;

	VTAILQ_INSERT_TAIL(&obj->store, sto, list);
	return (vsb);
}

void
SMS_Finish(struct object *obj)
{
	struct storage *sto;
	struct vsb *vsb;

	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);
	sto = VTAILQ_FIRST(&obj->store);
	assert(sto->stevedore == &sms_stevedore);
	vsb = sto->priv;
	vsb_finish(vsb);
	AZ(vsb_overflowed(vsb));

	sto->ptr = (void*)vsb_data(vsb);
	sto->len = vsb_len(vsb);
	sto->space = vsb_len(vsb);
	obj->len = sto->len;
	Lck_Lock(&sms_mtx);
	VSC_main->sms_nbytes += sto->len;
	VSC_main->sms_balloc += sto->len;
	Lck_Unlock(&sms_mtx);
}
