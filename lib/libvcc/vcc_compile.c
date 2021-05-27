/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 */

/*
 * XXX:
 *	Better error messages, throughout.
 *	>It also occurred to me that we could link the errors to the error
 *	>documentation.
 *	>
 *	>Unreferenced  function 'request_policy', first mention is
 *	>         Line 8 Pos 4
 *	>         sub request_policy {
 *	>         ----##############--
 *	>Read more about this type of error:
 *	>http://varnish/doc/error.html#Unreferenced%20function
 *	>
 *	>
 *	>         Unknown variable 'obj.bandwidth'
 *	>         At: Line 88 Pos 12
 *	>                 if (obj.bandwidth < 1 kb/h) {
 *	>         ------------#############------------
 *	>Read more about this type of error:
 *	>http://varnish/doc/error.html#Unknown%20variable
 *
 */

#include "config.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vcc_compile.h"

#include "libvcc.h"
#include "vfil.h"

static const struct method method_tab[] = {
	{ "none", 0U, 0},
#define VCL_MET_MAC(l,U,t,b)	{ "vcl_"#l, b, VCL_MET_##U },
#include "tbl/vcl_returns.h"
	{ NULL, 0U, 0}
};

struct vcc *vcc_builtin;

/*--------------------------------------------------------------------*/

static void
vcc_vcl_met2c(struct vsb *vsb, unsigned method)
{
	int d = 0;

	//lint -e{774} Boolean within 'if' always evaluates to False
#define VCL_MET_MAC(l,U,t,b)			\
	if (method & VCL_MET_##U) {		\
		if (d)				\
			VSB_putc(vsb, '|');	\
		VSB_cat(vsb, "VCL_MET_" #U);	\
		d = 1;				\
	}
#include "tbl/vcl_returns.h"
	AN(d);
}


/*--------------------------------------------------------------------*/

void * v_matchproto_(TlAlloc)
TlAlloc(struct vcc *tl, unsigned len)
{
	void *p;

	(void)tl;
	p = calloc(1, len);
	assert(p != NULL);
	return (p);
}

char *
TlDup(struct vcc *tl, const char *s)
{
	char *p;

	p = TlAlloc(tl, strlen(s) + 1);
	AN(p);
	strcpy(p, s);
	return (p);
}

static int
TLWriteVSB(struct vcc *tl, const char *fn, const struct vsb *vsb,
    const char *what)
{
	int fo;
	int i;

	fo = open(fn, O_WRONLY|O_TRUNC|O_CREAT, 0600);
	if (fo < 0) {
		VSB_printf(tl->sb,
		    "Could not open %s file %s: %s\n",
		    what, fn, strerror(errno));
		return (-1);
	}
	i = VSB_tofile(vsb, fo);
	if (i) {
		VSB_printf(tl->sb,
		    "Could not write %s to %s: %s\n",
		    what, fn, strerror(errno));
	}
	closefd(&fo);
	return (i);
}

/*--------------------------------------------------------------------*/

struct proc *
vcc_NewProc(struct vcc *tl, struct symbol *sym)
{
	struct proc *p;

	ALLOC_OBJ(p, PROC_MAGIC);
	AN(p);
	VTAILQ_INIT(&p->calls);
	VTAILQ_INIT(&p->uses);
	VTAILQ_INIT(&p->priv_tasks);
	VTAILQ_INIT(&p->priv_tops);
	VTAILQ_INSERT_TAIL(&tl->procs, p, list);
	p->prologue = VSB_new_auto();
	AN(p->prologue);
	p->body = VSB_new_auto();
	AN(p->body);
	p->cname = VSB_new_auto();
	AN(p->cname);
	p->okmask = VCL_MET_TASK_ALL;
	sym->proc = p;
	p->sym = sym;
	return (p);
}

static void
vcc_EmitProc(struct vcc *tl, struct proc *p)
{
	struct vsb *vsbm;
	unsigned mask, nsub;
	const char *maskcmp;
	const char *cc_adv;
	int dyn = (p->sym->nref > p->called);

	AN(p->okmask);
	AZ(VSB_finish(p->cname));
	AZ(VSB_finish(p->prologue));
	AZ(VSB_finish(p->body));
	AN(p->sym);

	if (p->method) {
		mask = p->method->bitval;
		maskcmp = "==";
	} else {
		mask = p->okmask;
		maskcmp = "&";
	}

	if (dyn == 0 && (p->calledfrom & VCL_MET_TASK_H) == p->calledfrom)
		cc_adv = "v_dont_optimize ";
	else
		cc_adv = "";

	nsub = tl->nsub++;

	Fh(tl, 1, "vcl_func_f %s;\n", VSB_data(p->cname));
	Fh(tl, 1, "extern const struct vcl_sub sub_%s[1];\n",
	    VSB_data(p->cname));
	Fh(tl, 1, "const struct vcl_sub sub_%s[1] = {{\n", VSB_data(p->cname));
	Fh(tl, 1, "\t.magic\t\t= VCL_SUB_MAGIC,\n");
	Fh(tl, 1, "\t.methods\t= 0x%x,\n", p->okmask);
	Fh(tl, 1, "\t.name\t\t= \"%.*s\",\n", PF(p->name));
	Fh(tl, 1, "\t.vcl_conf\t= &VCL_conf,\n");
	Fh(tl, 1, "\t.func\t\t= %s,\n", VSB_data(p->cname));
	Fh(tl, 1, "\t.n\t\t= %d,\n", nsub);
	Fh(tl, 1, "\t.nref\t\t= %d,\n", p->sym->nref);
	Fh(tl, 1, "\t.called\t\t= %d\n", p->called);
	Fh(tl, 1, "\t// calledfrom\t  0x%x\n", p->calledfrom);
	Fh(tl, 1, "}};\n");

	if (dyn) {
		Fc(tl, 1, "\nstatic inline void %s\n", cc_adv);
		Fc(tl, 1, "%s_checked(VRT_CTX)\n{\n", VSB_data(p->cname));
	} else {
		Fc(tl, 1, "\nvoid %sv_matchproto_(vcl_func_f)\n", cc_adv);
		Fc(tl, 1, "%s(VRT_CTX, enum vcl_func_call_e call,\n",
		    VSB_data(p->cname));
		Fc(tl, 1, "    enum vcl_func_fail_e *failp)\n{\n");
		Fc(tl, 1, "  assert(call == VSUB_STATIC);\n");
		Fc(tl, 1, "  assert(failp == NULL);\n");
	}

	vsbm = VSB_new_auto();
	AN(vsbm);
	vcc_vcl_met2c(vsbm, mask);
	AZ(VSB_finish(vsbm));
	Fc(tl, 1, "  assert(ctx->method %s (%s));\n", maskcmp, VSB_data(vsbm));
	VSB_destroy(&vsbm);
	Fc(tl, 1, "%s\n%s}\n", VSB_data(p->prologue), VSB_data(p->body));
	VSB_destroy(&p->body);
	VSB_destroy(&p->prologue);

	if (! dyn) {
		VSB_destroy(&p->cname);
		return;
	}

	/* wrapper to call the actual (_checked) function */
	Fc(tl, 1, "\nvoid v_matchproto_(vcl_func_f)\n");
	Fc(tl, 1, "%s(VRT_CTX, enum vcl_func_call_e call,\n",
	    VSB_data(p->cname));
	Fc(tl, 1, "    enum vcl_func_fail_e *failp)\n{\n");
	Fc(tl, 1, "  enum vcl_func_fail_e fail;\n\n");
	Fc(tl, 1, "  fail = VPI_Call_Check(ctx, &VCL_conf, 0x%x, %d);\n",
	    mask, nsub);
	Fc(tl, 1, "  if (failp)\n");
	Fc(tl, 1, "    *failp = fail;\n");
	Fc(tl, 1, "  else if (fail == VSUB_E_METHOD)\n");
	Fc(tl, 1, "    VRT_fail(ctx, \"call to \\\"sub %.*s{}\\\""
	    " not allowed from here\");\n", PF(p->name));
	Fc(tl, 1, "  else if (fail == VSUB_E_RECURSE)\n");
	Fc(tl, 1, "    VRT_fail(ctx, \"Recursive call to "
	    "\\\"sub %.*s{}\\\"\");\n", PF(p->name));
	Fc(tl, 1, "  else\n");
	Fc(tl, 1, "    assert (fail == VSUB_E_OK);\n");
	Fc(tl, 1, "  if (fail != VSUB_E_OK || call == VSUB_CHECK)\n");
	Fc(tl, 1, "    return;\n");
	Fc(tl, 1, "  VPI_Call_Begin(ctx, %d);\n", nsub);
	Fc(tl, 1, "  %s_checked(ctx);\n", VSB_data(p->cname));
	Fc(tl, 1, "  VPI_Call_End(ctx, %d);\n", nsub);
	Fc(tl, 1, "}\n");
	VSB_destroy(&p->cname);
}

/*--------------------------------------------------------------------*/

struct inifin *
New_IniFin(struct vcc *tl)
{
	struct inifin *p;

	ALLOC_OBJ(p, INIFIN_MAGIC);
	AN(p);
	p->ini = VSB_new_auto();
	AN(p->ini);
	p->fin = VSB_new_auto();
	AN(p->fin);
	p->final = VSB_new_auto();
	AN(p->final);
	p->event = VSB_new_auto();
	AN(p->event);
	p->n = ++tl->ninifin;
	VTAILQ_INSERT_TAIL(&tl->inifin, p, list);
	return (p);
}

/*--------------------------------------------------------------------
 * Printf output to the vsbs, possibly indented
 */

void
Fh(const struct vcc *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	if (indent)
		VSB_printf(tl->fh, "%*.*s", tl->hindent, tl->hindent, "");
	va_start(ap, fmt);
	VSB_vprintf(tl->fh, fmt, ap);
	va_end(ap);
}

void
Fb(const struct vcc *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	assert(tl->fb != NULL);
	if (indent)
		VSB_printf(tl->fb, "%*.*s", tl->indent, tl->indent, "");
	va_start(ap, fmt);
	VSB_vprintf(tl->fb, fmt, ap);
	va_end(ap);
}

void
Fc(const struct vcc *tl, int indent, const char *fmt, ...)
{
	va_list ap;

	if (indent)
		VSB_printf(tl->fc, "%*.*s", tl->indent, tl->indent, "");
	va_start(ap, fmt);
	VSB_vprintf(tl->fc, fmt, ap);
	va_end(ap);
}

/*--------------------------------------------------------------------*/

void
EncToken(struct vsb *sb, const struct token *t)
{

	assert(t->tok == CSTR);
	VSB_quote(sb, t->dec, -1, VSB_QUOTE_CSTR);
}

/*--------------------------------------------------------------------
 * Output the location/profiling table.  For each counted token, we
 * record source+line+charpos for the first character in the token.
 */

static void
EmitCoordinates(const struct vcc *tl, struct vsb *vsb)
{
	struct token *t;
	unsigned lin, pos;
	const struct source *sp;
	const char *p;

	VSB_cat(vsb, "/* ---===### Source Code ###===---*/\n");

	VSB_printf(vsb, "\n#define VGC_NSRCS %u\n", tl->nsources);

	VSB_cat(vsb, "\nstatic const char *srcname[VGC_NSRCS] = {\n");
	VTAILQ_FOREACH(sp, &tl->sources, list) {
		VSB_cat(vsb, "\t");
		VSB_quote(vsb, sp->name, -1, VSB_QUOTE_CSTR);
		VSB_cat(vsb, ",\n");
	}
	VSB_cat(vsb, "};\n");

	VSB_printf(vsb, "\nstatic const char *srcbody[%u] = {\n", tl->nsources);
	VTAILQ_FOREACH(sp, &tl->sources, list) {
		VSB_cat(vsb, "    /* ");
		VSB_quote(vsb, sp->name, -1, VSB_QUOTE_CSTR);
		VSB_cat(vsb, " */\n");
		VSB_quote_pfx(vsb, "\t", sp->b, sp->e - sp->b, VSB_QUOTE_CSTR);
		VSB_cat(vsb, ",\n");
	}
	VSB_cat(vsb, "};\n\n");

	VSB_cat(vsb, "/* ---===### Location Counters ###===---*/\n");

	VSB_printf(vsb, "\n#define VGC_NREFS %u\n\n", tl->cnt + 1);

	VSB_cat(vsb, "static const struct vpi_ref VGC_ref[VGC_NREFS] = {\n");
	lin = 1;
	pos = 0;
	sp = 0;
	p = NULL;
	VTAILQ_FOREACH(t, &tl->tokens, list) {
		if (t->b == NULL || t->cnt == 0)
			continue;
		assert(t->src != NULL);
		if (t->src != sp) {
			lin = 1;
			pos = 0;
			sp = t->src;
			p = sp->b;
		}
		assert(sp != NULL);
		assert(p != NULL);
		for (;p < t->b; p++) {
			if (*p == '\n') {
				lin++;
				pos = 0;
			} else if (*p == '\t') {
				pos &= ~7;
				pos += 8;
			} else
				pos++;

		}
		VSB_printf(vsb, "  [%3u] = { %u, %8tu, %4u, %3u, ",
		    t->cnt, sp->idx, t->b - sp->b, lin, pos + 1);
		if (t->tok == CSRC)
			VSB_cat(vsb, " \"C{\"},\n");
		else
			VSB_printf(vsb, " \"%.*s\" },\n", PF(t));
	}
	VSB_cat(vsb, "};\n\n");
}

/*--------------------------------------------------------------------
 * Init/Fini/Event
 *
 * We call DISCARD and COLD events in the opposite order of LOAD and
 * WARM.
 */

static void
EmitInitFini(const struct vcc *tl)
{
	struct inifin *p, *q = NULL;
	unsigned has_event = 0;
	struct symbol *sy;

	Fh(tl, 0, "\n");
	Fh(tl, 0, "static unsigned vgc_inistep;\n");
	Fh(tl, 0, "static unsigned vgc_warmupstep;\n");

	/*
	 * LOAD
	 */
	Fc(tl, 0, "\nstatic int\nVGC_Load(VRT_CTX)\n{\n\n");
	Fc(tl, 0, "\tvgc_inistep = 0;\n");
	Fc(tl, 0, "\tsize_t ndirector = %dUL;\n", tl->ndirector);
	Fc(tl, 0, "\n");
	VTAILQ_FOREACH(p, &tl->inifin, list) {
		AZ(VSB_finish(p->ini));
		assert(p->n > 0);
		if (VSB_len(p->ini))
			Fc(tl, 0, "\t/* %u */\n%s\n", p->n, VSB_data(p->ini));
		if (p->ignore_errors == 0) {
			Fc(tl, 0, "\tif (*ctx->handling == VCL_RET_FAIL)\n");
			Fc(tl, 0, "\t\treturn(1);\n");
		}
		Fc(tl, 0, "\tvgc_inistep = %u;\n\n", p->n);
		VSB_destroy(&p->ini);

		AZ(VSB_finish(p->event));
		if (VSB_len(p->event))
			has_event = 1;
	}

	/* Handle failures from vcl_init */
	Fc(tl, 0, "\n");
	Fc(tl, 0, "\tif (*ctx->handling != VCL_RET_OK)\n");
	Fc(tl, 0, "\t\treturn(1);\n");
	Fc(tl, 0, "\t*ctx->handling = 0;\n");

	VTAILQ_FOREACH(sy, &tl->sym_objects, sideways) {
		Fc(tl, 0, "\tif (!%s) {\n", sy->rname);
		Fc(tl, 0, "\t\tVRT_fail(ctx, "
		    "\"Object %s not initialized\");\n" , sy->name);
		Fc(tl, 0, "\t\treturn(1);\n");
		Fc(tl, 0, "\t}\n");
	}

	Fc(tl, 0, "\treturn(0);\n");
	Fc(tl, 0, "}\n");

	/*
	 * DISCARD
	 */
	Fc(tl, 0, "\nstatic int\nVGC_Discard(VRT_CTX)\n{\n\n");

	Fc(tl, 0, "\tswitch (vgc_inistep) {\n");
	VTAILQ_FOREACH_REVERSE(p, &tl->inifin, inifinhead, list) {
		AZ(VSB_finish(p->fin));
		if (q)
			assert(q->n > p->n);
		q = p;
		Fc(tl, 0, "\t\tcase %u:\n", p->n);
		if (VSB_len(p->fin))
			Fc(tl, 0, "\t%s\n", VSB_data(p->fin));
		Fc(tl, 0, "\t\t\t/* FALLTHROUGH */\n");
		VSB_destroy(&p->fin);
	}
	Fc(tl, 0, "\t\tdefault:\n\t\t\tbreak;\n");
	Fc(tl, 0, "\t}\n\n");
	Fc(tl, 0, "\tswitch (vgc_inistep) {\n");
	VTAILQ_FOREACH_REVERSE(p, &tl->inifin, inifinhead, list) {
		AZ(VSB_finish(p->final));
		Fc(tl, 0, "\t\tcase %u:\n", p->n);
		if (VSB_len(p->final))
			Fc(tl, 0, "\t%s\n", VSB_data(p->final));
		Fc(tl, 0, "\t\t\t/* FALLTHROUGH */\n");
		VSB_destroy(&p->final);
	}
	Fc(tl, 0, "\t\tdefault:\n\t\t\tbreak;\n");
	Fc(tl, 0, "\t}\n\n");

	Fc(tl, 0, "\treturn (0);\n");
	Fc(tl, 0, "}\n");

	if (has_event) {
		/*
		 * WARM
		 */
		Fc(tl, 0, "\nstatic int\n");
		Fc(tl, 0, "VGC_Warmup(VRT_CTX, enum vcl_event_e ev)\n{\n\n");

		Fc(tl, 0, "\tvgc_warmupstep = 0;\n\n");
		VTAILQ_FOREACH(p, &tl->inifin, list) {
			assert(p->n > 0);
			if (VSB_len(p->event)) {
				Fc(tl, 0, "\t/* %u */\n", p->n);
				Fc(tl, 0, "\tif (%s)\n", VSB_data(p->event));
				Fc(tl, 0, "\t\treturn (1);\n");
				Fc(tl, 0, "\tvgc_warmupstep = %u;\n\n", p->n);
			}
		}

		Fc(tl, 0, "\treturn (0);\n");
		Fc(tl, 0, "}\n");

		/*
		 * COLD
		 */
		Fc(tl, 0, "\nstatic int\n");
		Fc(tl, 0, "VGC_Cooldown(VRT_CTX, enum vcl_event_e ev)\n{\n");
		Fc(tl, 0, "\tint retval = 0;\n\n");

		VTAILQ_FOREACH_REVERSE(p, &tl->inifin, inifinhead, list) {
			if (VSB_len(p->event)) {
				Fc(tl, 0, "\t/* %u */\n", p->n);
				Fc(tl, 0,
				    "\tif (vgc_warmupstep >= %u &&\n", p->n);
				Fc(tl, 0,
				    "\t    %s != 0)\n", VSB_data(p->event));
				Fc(tl, 0, "\t\tretval = 1;\n\n");
			}
			VSB_destroy(&p->event);
		}

		Fc(tl, 0, "\treturn (retval);\n");
		Fc(tl, 0, "}\n");
	}

	/*
	 * EVENTS
	 */
	Fc(tl, 0, "\nstatic int\n");
	Fc(tl, 0, "VGC_Event(VRT_CTX, enum vcl_event_e ev)\n");
	Fc(tl, 0, "{\n");
	Fc(tl, 0, "\tif (ev == VCL_EVENT_LOAD)\n");
	Fc(tl, 0, "\t\treturn (VGC_Load(ctx));\n");
	if (has_event) {
		Fc(tl, 0, "\tif (ev == VCL_EVENT_WARM)\n");
		Fc(tl, 0, "\t\treturn (VGC_Warmup(ctx, ev));\n");
		Fc(tl, 0, "\tif (ev == VCL_EVENT_COLD)\n");
		Fc(tl, 0, "\t\treturn (VGC_Cooldown(ctx, ev));\n");
	}
	Fc(tl, 0, "\tif (ev == VCL_EVENT_DISCARD)\n");
	Fc(tl, 0, "\t\treturn (VGC_Discard(ctx));\n");
	Fc(tl, 0, "\n");
	if (!has_event)
		Fc(tl, 0, "\t(void)vgc_warmupstep;\n");
	Fc(tl, 0, "\treturn (%d);\n", has_event ? 1 : 0);
	Fc(tl, 0, "}\n");
}

/*--------------------------------------------------------------------*/

static void
EmitStruct(const struct vcc *tl)
{
	Fc(tl, 0, "\nconst struct VCL_conf VCL_conf = {\n");
	Fc(tl, 0, "\t.magic = VCL_CONF_MAGIC,\n");
	Fc(tl, 0, "\t.syntax = %u,\n", tl->syntax);
	Fc(tl, 0, "\t.event_vcl = VGC_Event,\n");
	Fc(tl, 0, "\t.default_director = &%s,\n", tl->default_director);
	if (tl->default_probe != NULL)
		Fc(tl, 0, "\t.default_probe = %s,\n", tl->default_probe);
	Fc(tl, 0, "\t.ref = VGC_ref,\n");
	Fc(tl, 0, "\t.nref = VGC_NREFS,\n");
	Fc(tl, 0, "\t.nsrc = VGC_NSRCS,\n");
	Fc(tl, 0, "\t.nsub = %d,\n", tl->subref > 0 ? tl->nsub : 0);
	Fc(tl, 0, "\t.srcname = srcname,\n");
	Fc(tl, 0, "\t.srcbody = srcbody,\n");
	Fc(tl, 0, "\t.nvmod = %u,\n", tl->vmod_count);
#define VCL_MET_MAC(l,u,t,b) \
	Fc(tl, 0, "\t." #l "_func = VGC_function_vcl_" #l ",\n");
#include "tbl/vcl_returns.h"
	Fc(tl, 0, "\t.instance_info = VGC_instance_info\n");
	Fc(tl, 0, "};\n");
}

/*--------------------------------------------------------------------
 * Compile the VCL code from the given source and return the C-source
 */

static struct vsb *
vcc_CompileSource(struct vcc *tl, struct source *sp, const char *jfile)
{
	struct proc *p;
	struct vsb *vsb;
	struct inifin *ifp;

	Fh(tl, 0, "/* ---===### VCC generated .h code ###===---*/\n");
	Fc(tl, 0, "\n/* ---===### VCC generated .c code ###===---*/\n");

	Fc(tl, 0, "\n#define END_ if (*ctx->handling) return\n");

	vcc_Parse_Init(tl);

	vcc_Expr_Init(tl);

	vcc_Action_Init(tl);

	vcc_Backend_Init(tl);

	vcc_Var_Init(tl);

	vcc_Type_Init(tl);

	Fh(tl, 0, "\nextern const struct VCL_conf VCL_conf;\n");

	/* Register and lex the main source */
	if (sp != NULL) {
		AN(vcc_builtin);
		vcc_lex_source(tl, sp, 0);
		if (tl->err)
			return (NULL);
	}

	/* Register and lex the builtin VCL */
	sp = vcc_new_source(tl->builtin_vcl, "builtin", "<builtin>");
	assert(sp != NULL);
	vcc_lex_source(tl, sp, 1);
	if (tl->err)
		return (NULL);

	/* Expand and lex any includes in the token string */
	if (tl->err)
		return (NULL);

	/* Parse the token string */
	tl->t = VTAILQ_FIRST(&tl->tokens);
	vcc_Parse(tl);
	if (tl->err)
		return (NULL);

	/* Check for orphans */
	if (vcc_CheckReferences(tl))
		return (NULL);

	/* Check that all action returns are legal */
	if (vcc_CheckAction(tl) || tl->err)
		return (NULL);

	/* Check that all variable uses are legal */
	if (vcc_CheckUses(tl) || tl->err)
		return (NULL);

	if (vcc_builtin == NULL)
		return (NULL);

	/* Check if we have any backends at all */
	if (tl->default_director == NULL) {
		VSB_cat(tl->sb,
		    "No backends or directors found in VCL program, "
		    "at least one is necessary.\n");
		tl->err = 1;
		return (NULL);
	}

	/* Tie vcl_init/fini in */
	ifp = New_IniFin(tl);
	VSB_cat(ifp->ini, "\tVGC_function_vcl_init(ctx, VSUB_STATIC, NULL);\n");
	/*
	 * Because the failure could be half way into vcl_init{} so vcl_fini{}
	 * must always be called, also on failure.
	 */
	ifp->ignore_errors = 1;
	VSB_cat(ifp->fin, "\t\tVGC_function_vcl_fini(ctx, VSUB_STATIC, NULL);\n");
	VSB_cat(ifp->fin, "\t\t\tVPI_vcl_fini(ctx);");

	/* Emit method functions */
	Fh(tl, 1, "\n");
	VTAILQ_FOREACH(p, &tl->procs, list)
		if (p->method == NULL)
			vcc_EmitProc(tl, p);
	VTAILQ_FOREACH(p, &tl->procs, list)
		if (p->method != NULL)
			vcc_EmitProc(tl, p);

	EmitInitFini(tl);

	VCC_InstanceInfo(tl);

	EmitStruct(tl);

	VCC_XrefTable(tl);

	VSB_cat(tl->symtab, "\n]\n");
	AZ(VSB_finish(tl->symtab));
	if (TLWriteVSB(tl, jfile, tl->symtab, "Symbol table"))
		return (NULL);

	/* Combine it all */

	vsb = VSB_new_auto();
	AN(vsb);

	vcl_output_lang_h(vsb);

	EmitCoordinates(tl, vsb);

	AZ(VSB_finish(tl->fh));
	VSB_cat(vsb, VSB_data(tl->fh));

	AZ(VSB_finish(tl->fc));
	VSB_cat(vsb, VSB_data(tl->fc));

	AZ(VSB_finish(vsb));
	return (vsb);
}

static struct vcc *
vcc_ParseBuiltin(struct vcc *tl)
{
	struct vcc *tl_builtin;

	CHECK_OBJ_NOTNULL(tl, VCC_MAGIC);
	tl_builtin = VCC_New();
	AN(tl_builtin);
	VCC_Builtin_VCL(tl_builtin, tl->builtin_vcl);
	AZ(vcc_CompileSource(tl_builtin, NULL, NULL));
	return (tl_builtin);
}

/*--------------------------------------------------------------------
 * Report the range of VCL language we support
 */
void
VCC_VCL_Range(unsigned *lo, unsigned *hi)
{

	AN(lo);
	*lo = VCL_LOW;
	AN(hi);
	*hi = VCL_HIGH;
}

/*--------------------------------------------------------------------
 * Compile the VCL code in the argument.  Error messages, if any are
 * formatted into the vsb.
 */

int
VCC_Compile(struct vcc *tl, struct vsb **sb,
    const char *vclsrc, const char *vclsrcfile,
    const char *ofile, const char *jfile)
{
	struct source *sp;
	struct vsb *r = NULL;
	int retval = 0;

	CHECK_OBJ_NOTNULL(tl, VCC_MAGIC);
	AN(sb);
	AN(vclsrcfile);
	AN(ofile);
	AN(jfile);

	AZ(vcc_builtin);
	vcc_builtin = vcc_ParseBuiltin(tl);
	AN(vcc_builtin);
	if (vcc_builtin->err) {
		AZ(VSB_finish(vcc_builtin->sb));
		*sb = vcc_builtin->sb;
		return (-1);
	}

	if (vclsrc != NULL)
		sp = vcc_new_source(vclsrc, "vcl.inline", vclsrcfile);
	else
		sp = vcc_file_source(tl, vclsrcfile);

	if (sp != NULL)
		r = vcc_CompileSource(tl, sp, jfile);

	if (r != NULL) {
		retval = TLWriteVSB(tl, ofile, r, "C-source");
		VSB_destroy(&r);
	} else {
		retval = -1;
	}
	AZ(VSB_finish(tl->sb));
	*sb = tl->sb;
	return (retval);
}

/*--------------------------------------------------------------------
 * Allocate a compiler instance
 */

struct vcc *
VCC_New(void)
{
	struct vcc *tl;
	struct symbol *sym;
	struct proc *p;
	int i;

	ALLOC_OBJ(tl, VCC_MAGIC);
	AN(tl);
	VTAILQ_INIT(&tl->vcl_syntax);
	VTAILQ_INIT(&tl->inifin);
	VTAILQ_INIT(&tl->tokens);
	VTAILQ_INIT(&tl->sources);
	VTAILQ_INIT(&tl->procs);
	VTAILQ_INIT(&tl->sym_objects);
	VTAILQ_INIT(&tl->sym_vmods);
	VTAILQ_INIT(&tl->vmod_objects);

	tl->nsources = 0;

	tl->symtab = VSB_new_auto();
	assert(tl->symtab != NULL);
	VSB_cat(tl->symtab, "[\n    {\"version\": 0}");

	tl->fc = VSB_new_auto();
	assert(tl->fc != NULL);

	tl->fh = VSB_new_auto();
	assert(tl->fh != NULL);

	for (i = 1; i < VCL_MET_MAX; i++) {
		sym = VCC_MkSym(tl, method_tab[i].name,
		    SYM_MAIN, SYM_SUB, VCL_LOW, VCL_HIGH);
		p = vcc_NewProc(tl, sym);
		p->method = &method_tab[i];
		VSB_printf(p->cname, "VGC_function_%s", p->method->name);
	}
	tl->sb = VSB_new_auto();
	AN(tl->sb);
	return (tl);
}

/*--------------------------------------------------------------------
 * Configure builtin VCL source code
 */

void
VCC_Builtin_VCL(struct vcc *vcc, const char *str)
{

	CHECK_OBJ_NOTNULL(vcc, VCC_MAGIC);
	REPLACE(vcc->builtin_vcl, str);
}

/*--------------------------------------------------------------------
 * Configure default VCL source path
 */

void
VCC_VCL_path(struct vcc *vcc, const char *str)
{

	CHECK_OBJ_NOTNULL(vcc, VCC_MAGIC);
	VFIL_setpath(&vcc->vcl_path, str);
}

/*--------------------------------------------------------------------
 * Configure default VMOD path
 */

void
VCC_VMOD_path(struct vcc *vcc, const char *str)
{

	CHECK_OBJ_NOTNULL(vcc, VCC_MAGIC);
	VFIL_setpath(&vcc->vmod_path, str);
}

/*--------------------------------------------------------------------
 * Configure settings
 */

#define MGT_VCC(type, name, camelcase)				\
	void VCC_ ## camelcase (struct vcc *vcc, type val)	\
	{							\
		CHECK_OBJ_NOTNULL(vcc, VCC_MAGIC);		\
		vcc->name = val;				\
	}
#include "tbl/mgt_vcc.h"

/*--------------------------------------------------------------------
 * Configure settings
 */

static void
vcc_predef_vcl(struct vcc *vcc, const char *name)
{
	struct symbol *sym;

	sym = VCC_MkSym(vcc, name, SYM_MAIN, SYM_VCL, VCL_LOW, VCL_HIGH);
	AN(sym);
	sym->type = VCL;
	sym->r_methods = VCL_MET_RECV;
}

void
VCC_Predef(struct vcc *vcc, const char *type, const char *name)
{

	CHECK_OBJ_NOTNULL(vcc, VCC_MAGIC);
	if (!strcmp(type, "VCL_STEVEDORE"))
		vcc_stevedore(vcc, name);
	else if (!strcmp(type, "VCL_VCL"))
		vcc_predef_vcl(vcc, name);
	else
		WRONG("Unknown VCC predef type");
}
