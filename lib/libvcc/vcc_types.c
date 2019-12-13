/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 */

#include "config.h"

#include <string.h>

#include "vcc_compile.h"

const struct type ACL[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"ACL",
	.tostring =		"((\v1)->name)",
}};

const struct type BACKEND[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"BACKEND",
	.tostring =		"VRT_BACKEND_string(\v1)",
}};

const struct type BLOB[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"BLOB",
}};

const struct type BODY[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"BODY",
}};

const struct type BOOL[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"BOOL",
	.tostring =		"VRT_BOOL_string(\v1)",
}};

const struct type BYTES[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"BYTES",
	.tostring =		"VRT_REAL_string(ctx, \v1)", // XXX: wrong
	.multype =		REAL,	// XXX: wrong
}};

const struct type DURATION[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"DURATION",
	.tostring =		"VRT_REAL_string(ctx, \v1)", // XXX 's' suff?
	.multype =		REAL,
}};

const struct type ENUM[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"ENUM",
	.tostring =		"",
}};

const struct type HEADER[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"HEADER",
	.tostring =		"VRT_GetHdr(ctx, \v1)",
}};

const struct type HTTP[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"HTTP",
}};

const struct type INSTANCE[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"INSTANCE",
}};

const struct type INT[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"INT",
	.multype =		INT,
	.tostring =		"VRT_INT_string(ctx, \v1)",
}};

const struct type IP[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"IP",
	.tostring =		"VRT_IP_string(ctx, \v1)",
}};

const struct type PROBE[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"PROBE",
}};

const struct type REAL[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"REAL",
	.tostring =		"VRT_REAL_string(ctx, \v1)",
	.multype =		REAL,
}};

static const struct vcc_method stevedore_methods[] = {
#define VRTSTVVAR(nm, vtype, ctype, dval) \
	{ VCC_METHOD_MAGIC, vtype, #nm, "VRT_stevedore_" #nm "(\v1)", 0},
#include "tbl/vrt_stv_var.h"
	{ VCC_METHOD_MAGIC, NULL },
};

const struct type STEVEDORE[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"STEVEDORE",
	.methods =		stevedore_methods,
	.tostring =		"VRT_STEVEDORE_string(\v1)",
}};

const struct type STRING[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"STRING",
	.stringform =		1,
}};

const struct type STRANDS[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"STRANDS",
	.stringform =		1,
	.tostring =		"VRT_CollectStrands(ctx,\v+\n\v1\v-\n)",
}};

static const struct vcc_method strings_methods[] = {
	{ VCC_METHOD_MAGIC, STRING, "upper",
	    "VRT_UpperLowerStrands(ctx, \vT, 1)", 1 },
	{ VCC_METHOD_MAGIC, STRING, "lower",
	    "VRT_UpperLowerStrands(ctx, \vT, 0)", 1 },
	{ VCC_METHOD_MAGIC, NULL },
};

const struct type STRINGS[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"STRINGS",
	.methods =		strings_methods,
	.tostring =		"",
}};

const struct type STRING_LIST[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"STRING_LIST",
	.stringform =		1,
}};

const struct type SUB[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"SUB",
}};

const struct type TIME[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"TIME",
	.tostring =		"VRT_TIME_string(ctx, \v1)",
}};

const struct type VCL[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"VCL",
}};

const struct type VOID[1] = {{
	.magic =		TYPE_MAGIC,
	.name =			"VOID",
}};

vcc_type_t
VCC_Type(const char *p)
{

#define VCC_TYPE(UC, lc)	if (!strcmp(p, #UC)) return (UC);
#include "tbl/vcc_types.h"
	return (NULL);
}

static void
vcc_type_init(struct vcc *tl, vcc_type_t type)
{
	const struct vcc_method *vm;
	struct symbol *sym;
	struct vsb *buf;

	/* NB: Don't bother even creating a type symbol if there are no
	 * methods attached to it.
	 */
	if (type->methods == NULL)
		return;

	buf = VSB_new_auto();
	AN(buf);
	VSB_printf(buf, "_type.%s", type->name);
	AZ(VSB_finish(buf));
	AN(VCC_MkSym(tl, VSB_data(buf), SYM_NONE, VCL_LOW, VCL_HIGH));

	for (vm = type->methods; vm->type != NULL; vm++) {
		VSB_clear(buf);
		VSB_printf(buf, "_type.%s.%s", type->name, vm->name);
		AZ(VSB_finish(buf));
		sym = VCC_MkSym(tl, VSB_data(buf), SYM_METHOD, VCL_LOW,
		    VCL_HIGH);
		if (tl->err)
			break;
		AN(sym);
		sym->type = vm->type;
		sym->eval_priv = vm;
	}

	VSB_destroy(&buf);
}

void
vcc_Type_Init(struct vcc *tl)
{

	AN(VCC_MkSym(tl, "_type", SYM_RESERVED, VCL_LOW, VCL_HIGH));

#define VCC_TYPE(UC, lc)	vcc_type_init(tl, UC);
#include "tbl/vcc_types.h"
}
