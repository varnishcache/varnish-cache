/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"

#include "vss.h"

/*--------------------------------------------------------------------
 * Struct sockaddr is not really designed to be a compile time
 * initialized data structure, so we encode it as a byte-string
 * and put it in an official sockaddr when we load the VCL.
 */

static void
Emit_Sockaddr(struct vcc *tl, const struct token *t_host, const char *port)
{
	const char *ipv4, *ipv4a, *ipv6, *ipv6a, *pa;
	const char *err;
	char *hop, *pop;

	AN(t_host->dec);

	err = VSS_parse(t_host->dec, &hop, &pop);
	if (err != NULL) {
		VSB_printf(tl->sb,
		    "Backend host '%.*s': %s\n", PF(t_host), err);
		vcc_ErrWhere(tl, t_host);
		return;
	}
	Resolve_Sockaddr(tl,
	    hop != NULL ? hop : t_host->dec,
	    pop != NULL ? pop : port,
	    &ipv4, &ipv4a, &ipv6, &ipv6a, &pa, 2, t_host, "Backend host");
	ERRCHK(tl);
	if (ipv4 != NULL) {
		Fb(tl, 0, "\t.ipv4_suckaddr = (const struct suckaddr *)%s,\n",
		    ipv4);
		Fb(tl, 0, "\t.ipv4_addr = \"%s\",\n", ipv4a);
	}
	if (ipv6 != NULL) {
		Fb(tl, 0, "\t.ipv6_suckaddr = (const struct suckaddr *)%s,\n",
		    ipv6);
		Fb(tl, 0, "\t.ipv6_addr = \"%s\",\n", ipv6a);
	}
	Fb(tl, 0, "\t.port = \"%s\",\n", pa);
}

/*--------------------------------------------------------------------
 * Parse a backend probe specification
 */

static void
vcc_ProbeRedef(struct vcc *tl, struct token **t_did,
    struct token *t_field)
{
	/* .url and .request are mutually exclusive */

	if (*t_did != NULL) {
		VSB_printf(tl->sb, "Probe request redefinition at:\n");
		vcc_ErrWhere(tl, t_field);
		VSB_printf(tl->sb, "Previous definition:\n");
		vcc_ErrWhere(tl, *t_did);
		return;
	}
	*t_did = t_field;
}

static void
vcc_ParseProbeSpec(struct vcc *tl)
{
	struct fld_spec *fs;
	struct token *t_field;
	struct token *t_did = NULL, *t_window = NULL, *t_threshold = NULL;
	struct token *t_initial = NULL;
	unsigned window, threshold, initial, status;
	double t;

	fs = vcc_FldSpec(tl,
	    "?url",
	    "?request",
	    "?expected_response",
	    "?timeout",
	    "?interval",
	    "?window",
	    "?threshold",
	    "?initial",
	    NULL);

	SkipToken(tl, '{');

	window = 0;
	threshold = 0;
	initial = 0;
	status = 0;
	Fh(tl, 0, "static const struct vrt_backend_probe vgc_probe__%d = {\n",
	    tl->nprobe++);
	while (tl->t->tok != '}') {

		vcc_IsField(tl, &t_field, fs);
		ERRCHK(tl);
		if (vcc_IdIs(t_field, "url")) {
			vcc_ProbeRedef(tl, &t_did, t_field);
			ERRCHK(tl);
			ExpectErr(tl, CSTR);
			Fh(tl, 0, "\t.url = ");
			EncToken(tl->fh, tl->t);
			Fh(tl, 0, ",\n");
			vcc_NextToken(tl);
		} else if (vcc_IdIs(t_field, "request")) {
			vcc_ProbeRedef(tl, &t_did, t_field);
			ERRCHK(tl);
			ExpectErr(tl, CSTR);
			Fh(tl, 0, "\t.request =\n");
			while (tl->t->tok == CSTR) {
				Fh(tl, 0, "\t\t");
				EncToken(tl->fh, tl->t);
				Fh(tl, 0, " \"\\r\\n\"\n");
				vcc_NextToken(tl);
			}
			Fh(tl, 0, "\t\t\"\\r\\n\",\n");
		} else if (vcc_IdIs(t_field, "timeout")) {
			Fh(tl, 0, "\t.timeout = ");
			vcc_Duration(tl, &t);
			ERRCHK(tl);
			Fh(tl, 0, "%g,\n", t);
		} else if (vcc_IdIs(t_field, "interval")) {
			Fh(tl, 0, "\t.interval = ");
			vcc_Duration(tl, &t);
			ERRCHK(tl);
			Fh(tl, 0, "%g,\n", t);
		} else if (vcc_IdIs(t_field, "window")) {
			t_window = tl->t;
			window = vcc_UintVal(tl);
			ERRCHK(tl);
		} else if (vcc_IdIs(t_field, "initial")) {
			t_initial = tl->t;
			initial = vcc_UintVal(tl);
			ERRCHK(tl);
		} else if (vcc_IdIs(t_field, "expected_response")) {
			status = vcc_UintVal(tl);
			if (status < 100 || status > 999) {
				VSB_printf(tl->sb,
				    "Must specify .expected_response with "
				    "exactly three digits "
				    "(100 <= x <= 999)\n");
				vcc_ErrWhere(tl, tl->t);
				return;
			}
			ERRCHK(tl);
		} else if (vcc_IdIs(t_field, "threshold")) {
			t_threshold = tl->t;
			threshold = vcc_UintVal(tl);
			ERRCHK(tl);
		} else {
			vcc_ErrToken(tl, t_field);
			vcc_ErrWhere(tl, t_field);
			ErrInternal(tl);
			return;
		}

		SkipToken(tl, ';');
	}

	if (t_threshold != NULL || t_window != NULL) {
		if (t_threshold == NULL && t_window != NULL) {
			VSB_printf(tl->sb,
			    "Must specify .threshold with .window\n");
			vcc_ErrWhere(tl, t_window);
			return;
		} else if (t_threshold != NULL && t_window == NULL) {
			if (threshold > 64) {
				VSB_printf(tl->sb,
				    "Threshold must be 64 or less.\n");
				vcc_ErrWhere(tl, t_threshold);
				return;
			}
			window = threshold + 1;
		} else if (window > 64) {
			AN(t_window);
			VSB_printf(tl->sb, "Window must be 64 or less.\n");
			vcc_ErrWhere(tl, t_window);
			return;
		}
		if (threshold > window ) {
			VSB_printf(tl->sb,
			    "Threshold can not be greater than window.\n");
			AN(t_threshold);
			vcc_ErrWhere(tl, t_threshold);
			AN(t_window);
			vcc_ErrWhere(tl, t_window);
		}
		Fh(tl, 0, "\t.window = %u,\n", window);
		Fh(tl, 0, "\t.threshold = %u,\n", threshold);
	}
	if (t_initial != NULL)
		Fh(tl, 0, "\t.initial = %u,\n", initial);
	else
		Fh(tl, 0, "\t.initial = ~0U,\n");
	if (status > 0)
		Fh(tl, 0, "\t.exp_status = %u,\n", status);
	Fh(tl, 0, "};\n");
	SkipToken(tl, '}');
}

/*--------------------------------------------------------------------
 * Parse and emit a probe definition
 */

void
vcc_ParseProbe(struct vcc *tl)
{
	struct token *t_probe;
	int i;

	vcc_NextToken(tl);		/* ID: probe */

	vcc_ExpectCid(tl);		/* ID: name */
	ERRCHK(tl);
	t_probe = tl->t;
	vcc_NextToken(tl);
	i = vcc_AddDef(tl, t_probe, SYM_PROBE);
	if (i > 1) {
		VSB_printf(tl->sb, "Probe %.*s redefined\n", PF(t_probe));
		vcc_ErrWhere(tl, t_probe);
	}

	Fh(tl, 0, "\n#define vgc_probe_%.*s\tvgc_probe__%d\n",
	    PF(t_probe), tl->nprobe);
	vcc_ParseProbeSpec(tl);
}

/*--------------------------------------------------------------------
 * Parse and emit a backend host definition
 *
 * The struct vrt_backend is emitted to Fh().
 */

static void
vcc_ParseHostDef(struct vcc *tl, const struct token *t_be)
{
	struct token *t_field;
	struct token *t_host = NULL;
	struct token *t_port = NULL;
	struct token *t_hosthdr = NULL;
	struct fld_spec *fs;
	struct inifin *ifp;
	struct vsb *vsb;
	unsigned u;
	double t;
	char vgcname[MAX_BACKEND_NAME + 8];

	sprintf(vgcname, "_%.*s", PF(t_be));

	Fh(tl, 1, "\n#define VGC_backend_%s %d\n", vgcname, tl->ndirector);

	fs = vcc_FldSpec(tl,
	    "!host",
	    "?port",
	    "?host_header",
	    "?connect_timeout",
	    "?first_byte_timeout",
	    "?between_bytes_timeout",
	    "?probe",
	    "?max_connections",
	    NULL);

	SkipToken(tl, '{');

	vsb = VSB_new_auto();
	AN(vsb);
	tl->fb = vsb;

	Fb(tl, 0, "\nstatic const struct vrt_backend vgc_dir_priv_%s = {\n",
	    vgcname);

	Fb(tl, 0, "\t.vcl_name = \"%.*s", PF(t_be));
	Fb(tl, 0, "\",\n");

	/* Check for old syntax */
	if (tl->t->tok == ID && vcc_IdIs(tl->t, "set")) {
		VSB_printf(tl->sb,
		    "NB: Backend Syntax has changed:\n"
		    "Remove \"set\" and \"backend\" in front"
		    " of backend fields.\n" );
		vcc_ErrToken(tl, tl->t);
		VSB_printf(tl->sb, " at ");
		vcc_ErrWhere(tl, tl->t);
		return;
	}

	while (tl->t->tok != '}') {

		vcc_IsField(tl, &t_field, fs);
		ERRCHK(tl);
		if (vcc_IdIs(t_field, "host")) {
			ExpectErr(tl, CSTR);
			assert(tl->t->dec != NULL);
			t_host = tl->t;
			vcc_NextToken(tl);
			SkipToken(tl, ';');
		} else if (vcc_IdIs(t_field, "port")) {
			ExpectErr(tl, CSTR);
			assert(tl->t->dec != NULL);
			t_port = tl->t;
			vcc_NextToken(tl);
			SkipToken(tl, ';');
		} else if (vcc_IdIs(t_field, "host_header")) {
			ExpectErr(tl, CSTR);
			assert(tl->t->dec != NULL);
			t_hosthdr = tl->t;
			vcc_NextToken(tl);
			SkipToken(tl, ';');
		} else if (vcc_IdIs(t_field, "connect_timeout")) {
			Fb(tl, 0, "\t.connect_timeout = ");
			vcc_Duration(tl, &t);
			ERRCHK(tl);
			Fb(tl, 0, "%g,\n", t);
			SkipToken(tl, ';');
		} else if (vcc_IdIs(t_field, "first_byte_timeout")) {
			Fb(tl, 0, "\t.first_byte_timeout = ");
			vcc_Duration(tl, &t);
			ERRCHK(tl);
			Fb(tl, 0, "%g,\n", t);
			SkipToken(tl, ';');
		} else if (vcc_IdIs(t_field, "between_bytes_timeout")) {
			Fb(tl, 0, "\t.between_bytes_timeout = ");
			vcc_Duration(tl, &t);
			ERRCHK(tl);
			Fb(tl, 0, "%g,\n", t);
			SkipToken(tl, ';');
		} else if (vcc_IdIs(t_field, "max_connections")) {
			u = vcc_UintVal(tl);
			ERRCHK(tl);
			SkipToken(tl, ';');
			Fb(tl, 0, "\t.max_connections = %u,\n", u);
		} else if (vcc_IdIs(t_field, "probe") && tl->t->tok == '{') {
			Fb(tl, 0, "\t.probe = &vgc_probe__%d,\n", tl->nprobe);
			vcc_ParseProbeSpec(tl);
			ERRCHK(tl);
		} else if (vcc_IdIs(t_field, "probe") && tl->t->tok == ID) {
			Fb(tl, 0, "\t.probe = &vgc_probe_%.*s,\n", PF(tl->t));
			vcc_AddRef(tl, tl->t, SYM_PROBE);
			vcc_NextToken(tl);
			SkipToken(tl, ';');
		} else if (vcc_IdIs(t_field, "probe")) {
			VSB_printf(tl->sb,
			    "Expected '{' or name of probe, got ");
			vcc_ErrToken(tl, tl->t);
			VSB_printf(tl->sb, " at\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		} else {
			ErrInternal(tl);
			return;
		}

	}

	vcc_FieldsOk(tl, fs);
	ERRCHK(tl);

	/* Check that the hostname makes sense */
	assert(t_host != NULL);
	if (t_port != NULL)
		Emit_Sockaddr(tl, t_host, t_port->dec);
	else
		Emit_Sockaddr(tl, t_host, "80");
	ERRCHK(tl);

	ExpectErr(tl, '}');

	/* We have parsed it all, emit the ident string */

	/* Emit the hosthdr field, fall back to .host if not specified */
	Fb(tl, 0, "\t.hosthdr = ");
	if (t_hosthdr != NULL)
		EncToken(tl->fb, t_hosthdr);
	else
		EncToken(tl->fb, t_host);
	Fb(tl, 0, ",\n");

	/* Close the struct */
	Fb(tl, 0, "};\n");

	vcc_NextToken(tl);

	tl->fb = NULL;
	AZ(VSB_finish(vsb));
	Fh(tl, 0, "%s", VSB_data(vsb));
	VSB_delete(vsb);

	ifp = New_IniFin(tl);
	VSB_printf(ifp->ini,
	    "\tVRT_init_dir(cli, VCL_conf.director,\n"
	    "\t    VGC_backend_%s, &vgc_dir_priv_%s);", vgcname, vgcname);
	VSB_printf(ifp->fin, "\tVRT_fini_dir(cli, VGCDIR(%s));", vgcname);
	tl->ndirector++;
}

/*--------------------------------------------------------------------
 * Parse directors and backends
 */

void
vcc_ParseBackend(struct vcc *tl)
{
	struct token *t_first, *t_be;
	int isfirst;
	struct symbol *sym;

	t_first = tl->t;
	vcc_NextToken(tl);		/* ID: backend */

	vcc_ExpectCid(tl);		/* ID: name */
	ERRCHK(tl);

	if (tl->t->e - tl->t->b > MAX_BACKEND_NAME) {
		VSB_printf(tl->sb,
		    "Name of %.*s too long (max %d, is %zu):\n",
		    PF(t_first), MAX_BACKEND_NAME,
		    (size_t)(tl->t->e - tl->t->b));
		vcc_ErrWhere(tl, tl->t);
		return;
	}

	t_be = tl->t;
	vcc_NextToken(tl);

	isfirst = tl->ndirector;

	sym = VCC_GetSymbolTok(tl, t_be, SYM_BACKEND);
	AN(sym);
	if (sym->ndef > 0) {
		VSB_printf(tl->sb, "Backend %.*s redefined\n", PF(t_be));
		vcc_ErrWhere(tl, t_be);
		return;
	}
	sym->fmt = BACKEND;
	sym->eval = vcc_Eval_Backend;
	sym->ndef++;
	ERRCHK(tl);

	vcc_ParseHostDef(tl, t_be);
	ERRCHK(tl);

	if (tl->err) {
		VSB_printf(tl->sb,
		    "\nIn %.*s specification starting at:\n", PF(t_first));
		vcc_ErrWhere(tl, t_first);
		return;
	}

	if (isfirst == 1 || vcc_IdIs(t_be, "default")) {
		tl->defaultdir = tl->ndirector - 1;
		tl->t_defaultdir = t_be;
	}
}
