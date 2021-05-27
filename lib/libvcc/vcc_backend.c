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
 *
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "vcc_compile.h"

const char *
vcc_default_probe(struct vcc *tl)
{

	if (tl->default_probe != NULL)
		return (tl->default_probe);
	VSB_cat(tl->sb, "No default probe defined\n");
	vcc_ErrToken(tl, tl->t);
	VSB_cat(tl->sb, " at\n");
	vcc_ErrWhere(tl, tl->t);
	return ("");
}

/*--------------------------------------------------------------------
 * Struct sockaddr is not really designed to be a compile time
 * initialized data structure, so we encode it as a byte-string
 * and put it in an official sockaddr when we load the VCL.
 */

static void
Emit_Sockaddr(struct vcc *tl, struct vsb *vsb1, const struct token *t_host,
    const struct token *t_port)
{
	const char *ipv4, *ipv4a, *ipv6, *ipv6a, *pa;
	char buf[BUFSIZ];

	AN(t_host->dec);

	if (t_port != NULL)
		bprintf(buf, "%s %s", t_host->dec, t_port->dec);
	else
		bprintf(buf, "%s", t_host->dec);
	Resolve_Sockaddr(tl, buf, "80",
	    &ipv4, &ipv4a, &ipv6, &ipv6a, &pa, 2, t_host, "Backend host");
	ERRCHK(tl);
	if (ipv4 != NULL) {
		VSB_printf(vsb1,
		    "\t.ipv4 = (const struct suckaddr *)%s,\n",
		    ipv4);
	}
	if (ipv6 != NULL) {
		VSB_printf(vsb1,
		    "\t.ipv6 = (const struct suckaddr *)%s,\n",
		    ipv6);
	}
	VSB_cat(vsb1, "\t.uds_path = (void *) 0,\n");
}

/*
 * For UDS, we do not create a VSA. Check if it's an absolute path, can
 * be accessed, and is a socket. If so, just emit the path field and set
 * the IP suckaddrs to NULL.
 */
static void
Emit_UDS_Path(struct vcc *tl, struct vsb *vsb1,
    const struct token *t_path, const char *errid)
{
	struct stat st;

	AN(t_path);
	AN(t_path->dec);

	if (*t_path->dec != '/') {
		VSB_printf(tl->sb,
			   "%s: Must be an absolute path:\n", errid);
		vcc_ErrWhere(tl, t_path);
		return;
	}
	errno = 0;
	if (stat(t_path->dec, &st) != 0) {
		int err = errno;
		VSB_printf(tl->sb, "%s: Cannot stat: %s\n", errid,
			   strerror(errno));
		vcc_ErrWhere(tl, t_path);
		if (err == ENOENT || err == EACCES)
			vcc_Warn(tl);
		else
			return;
	} else if (!S_ISSOCK(st.st_mode)) {
		VSB_printf(tl->sb, "%s: Not a socket:\n", errid);
		vcc_ErrWhere(tl, t_path);
		return;
	}
	VSB_printf(vsb1, "\t.uds_path = \"%s\",\n", t_path->dec);
	VSB_cat(vsb1, "\t.ipv4 = (void *) 0,\n");
	VSB_cat(vsb1, "\t.ipv6 = (void *) 0,\n");
}

/*--------------------------------------------------------------------
 * Disallow mutually exclusive field definitions
 */

static void
vcc_Redef(struct vcc *tl, const char *redef, const struct token **t_did,
    const struct token *t_field)
{
	if (*t_did != NULL) {
		VSB_printf(tl->sb, "%s redefinition at:\n", redef);
		vcc_ErrWhere(tl, t_field);
		VSB_cat(tl->sb, "Previous definition:\n");
		vcc_ErrWhere(tl, *t_did);
		return;
	}
	*t_did = t_field;
}

/*--------------------------------------------------------------------
 * Parse a backend probe specification
 */

static void
vcc_ParseProbeSpec(struct vcc *tl, const struct symbol *sym, char **namep)
{
	struct fld_spec *fs;
	const struct token *t_field;
	const struct token *t_did = NULL, *t_window = NULL, *t_threshold = NULL;
	struct token *t_initial = NULL;
	unsigned window, threshold, initial, status;
	char buf[32];
	const char *name;
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

	if (sym != NULL) {
		name = sym->rname;
	} else {
		bprintf(buf, "vgc_probe__%d", tl->nprobe++);
		name = buf;
	}
	Fh(tl, 0, "static const struct vrt_backend_probe %s[] = {{\n", name);
	Fh(tl, 0, "\t.magic = VRT_BACKEND_PROBE_MAGIC,\n");
	if (namep != NULL)
		*namep = TlDup(tl, name);

	window = 0;
	threshold = 0;
	initial = 0;
	status = 0;
	while (tl->t->tok != '}') {

		vcc_IsField(tl, &t_field, fs);
		ERRCHK(tl);
		if (vcc_IdIs(t_field, "url")) {
			vcc_Redef(tl, "Probe request", &t_did, t_field);
			ERRCHK(tl);
			ExpectErr(tl, CSTR);
			Fh(tl, 0, "\t.url = ");
			EncToken(tl->fh, tl->t);
			Fh(tl, 0, ",\n");
			vcc_NextToken(tl);
		} else if (vcc_IdIs(t_field, "request")) {
			vcc_Redef(tl, "Probe request", &t_did, t_field);
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
				VSB_cat(tl->sb,
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
	free(fs);

	if (t_threshold != NULL || t_window != NULL) {
		if (t_threshold == NULL && t_window != NULL) {
			VSB_cat(tl->sb,
			    "Must specify .threshold with .window\n");
			vcc_ErrWhere(tl, t_window);
			return;
		} else if (t_threshold != NULL && t_window == NULL) {
			if (threshold > 64) {
				VSB_cat(tl->sb,
				    "Threshold must be 64 or less.\n");
				vcc_ErrWhere(tl, t_threshold);
				return;
			}
			window = threshold + 1;
		} else if (window > 64) {
			AN(t_window);
			VSB_cat(tl->sb, "Window must be 64 or less.\n");
			vcc_ErrWhere(tl, t_window);
			return;
		}
		if (threshold > window ) {
			VSB_cat(tl->sb,
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
	Fh(tl, 0, "}};\n");
	SkipToken(tl, '}');
}

/*--------------------------------------------------------------------
 * Parse and emit a probe definition
 */

void
vcc_ParseProbe(struct vcc *tl)
{
	struct symbol *sym;
	char *p;

	vcc_NextToken(tl);			/* ID: probe */

	vcc_ExpectVid(tl, "backend probe");	/* ID: name */
	ERRCHK(tl);
	if (vcc_IdIs(tl->t, "default")) {
		vcc_NextToken(tl);
		vcc_ParseProbeSpec(tl, NULL, &p);
		tl->default_probe = p;
	} else {
		sym = VCC_HandleSymbol(tl, PROBE);
		ERRCHK(tl);
		AN(sym);
		vcc_ParseProbeSpec(tl, sym, NULL);
	}
}

/*--------------------------------------------------------------------
 * Parse and emit a backend host definition
 *
 * The struct vrt_backend is emitted to Fh().
 */

static void
vcc_ParseHostDef(struct vcc *tl, const struct token *t_be, const char *vgcname)
{
	const struct token *t_field;
	const struct token *t_val;
	const struct token *t_host = NULL;
	const struct token *t_port = NULL;
	const struct token *t_path = NULL;
	const struct token *t_hosthdr = NULL;
	const struct token *t_did = NULL;
	const struct token *t_preamble = NULL;
	struct symbol *pb;
	struct fld_spec *fs;
	struct inifin *ifp;
	struct vsb *vsb1;
	char *p;
	unsigned u;
	double t;

	if (tl->t->tok == ID &&
	    (vcc_IdIs(tl->t, "none") || vcc_IdIs(tl->t, "None"))) {
		vcc_NextToken(tl);
		SkipToken(tl, ';');
		ifp = New_IniFin(tl);
		VSB_printf(ifp->ini, "\t(void)%s;", vgcname);
		VSB_printf(ifp->fin, "\t\t(void)%s;", vgcname);
		return;
	}

	SkipToken(tl, '{');

	/* Check for old syntax */
	if (tl->t->tok == ID && vcc_IdIs(tl->t, "set")) {
		VSB_cat(tl->sb,
		    "NB: Backend Syntax has changed:\n"
		    "Remove \"set\" and \"backend\" in front"
		    " of backend fields.\n" );
		vcc_ErrToken(tl, tl->t);
		VSB_cat(tl->sb, " at ");
		vcc_ErrWhere(tl, tl->t);
		return;
	}

	fs = vcc_FldSpec(tl,
	    "?host",
	    "?port",
	    "?path",
	    "?host_header",
	    "?connect_timeout",
	    "?first_byte_timeout",
	    "?between_bytes_timeout",
	    "?probe",
	    "?max_connections",
	    "?proxy_header",
	    "?preamble",
	    NULL);

	tl->fb = VSB_new_auto();
	AN(tl->fb);

	Fb(tl, 0, "\nstatic const struct vrt_backend vgc_dir_priv_%s = {\n",
	    vgcname);

	Fb(tl, 0, "\t.magic = VRT_BACKEND_MAGIC,\n");
	Fb(tl, 0, "\t.endpoint = &vgc_dir_ep_%s,\n", vgcname);
	Fb(tl, 0, "\t.vcl_name = \"%.*s", PF(t_be));
	Fb(tl, 0, "\",\n");

	while (tl->t->tok != '}') {

		vcc_IsField(tl, &t_field, fs);
		ERRCHK(tl);
		if (vcc_IdIs(t_field, "host")) {
			vcc_Redef(tl, "Address", &t_did, t_field);
			ERRCHK(tl);
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
		} else if (vcc_IdIs(t_field, "path")) {
			if (tl->esyntax < VCL_41) {
				VSB_cat(tl->sb,
				    "Unix socket backends only supported"
				    " in VCL4.1 and higher.\n");
				vcc_ErrToken(tl, tl->t);
				VSB_cat(tl->sb, " at ");
				vcc_ErrWhere(tl, tl->t);
				VSB_destroy(&tl->fb);
				return;
			}
			vcc_Redef(tl, "Address", &t_did, t_field);
			ERRCHK(tl);
			ExpectErr(tl, CSTR);
			assert(tl->t->dec != NULL);
			t_path = tl->t;
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
		} else if (vcc_IdIs(t_field, "proxy_header")) {
			t_val = tl->t;
			u = vcc_UintVal(tl);
			ERRCHK(tl);
			if (u != 1 && u != 2) {
				VSB_cat(tl->sb,
				    ".proxy_header must be 1 or 2\n");
				vcc_ErrWhere(tl, t_val);
				VSB_destroy(&tl->fb);
				return;
			}
			SkipToken(tl, ';');
			Fb(tl, 0, "\t.proxy_header = %u,\n", u);
		} else if (vcc_IdIs(t_field, "probe") && tl->t->tok == '{') {
			vcc_ParseProbeSpec(tl, NULL, &p);
			Fb(tl, 0, "\t.probe = %s,\n", p);
			free(p);
			ERRCHK(tl);
		} else if (vcc_IdIs(t_field, "probe") && tl->t->tok == ID) {
			if (vcc_IdIs(tl->t, "default")) {
				vcc_NextToken(tl);
				(void)vcc_default_probe(tl);
			} else {
				pb = VCC_SymbolGet(tl, SYM_MAIN, SYM_PROBE,
				    SYMTAB_EXISTING, XREF_REF);
				ERRCHK(tl);
				AN(pb);
				Fb(tl, 0, "\t.probe = %s,\n", pb->rname);
			}
			SkipToken(tl, ';');
		} else if (vcc_IdIs(t_field, "probe")) {
			VSB_cat(tl->sb, "Expected '{' or name of probe, got ");
			vcc_ErrToken(tl, tl->t);
			VSB_cat(tl->sb, " at\n");
			vcc_ErrWhere(tl, tl->t);
			VSB_destroy(&tl->fb);
			return;
		} else if (vcc_IdIs(t_field, "preamble")) {
			ExpectErr(tl, CBLOB);
			t_preamble = tl->t;
			vcc_NextToken(tl);
			SkipToken(tl, ';');
		} else {
			ErrInternal(tl);
			VSB_destroy(&tl->fb);
			return;
		}

	}

	vcc_FieldsOk(tl, fs);
	free(fs);
	ERRCHK(tl);

	ExpectErr(tl, '}');

	if (t_host == NULL && t_path == NULL) {
		VSB_cat(tl->sb, "Expected .host or .path.\n");
		vcc_ErrWhere(tl, t_be);
		VSB_destroy(&tl->fb);
		return;
	}

	vsb1 = VSB_new_auto();
	AN(vsb1);
	VSB_printf(vsb1,
	    "\nstatic const struct vrt_endpoint vgc_dir_ep_%s = {\n",
	    vgcname);
	VSB_cat(vsb1, "\t.magic = VRT_ENDPOINT_MAGIC,\n");

	assert(t_host != NULL || t_path != NULL);
	if (t_host != NULL)
		/* Check that the hostname makes sense */
		Emit_Sockaddr(tl, vsb1, t_host, t_port);
	else
		/* Check that the path can be a legal UDS */
		Emit_UDS_Path(tl, vsb1, t_path, "Backend path");
	ERRCHK(tl);

	if (t_preamble != NULL)
		VSB_printf(vsb1, "\t.preamble = %s,\n", t_preamble->dec);

	VSB_cat(vsb1, "};\n");
	AZ(VSB_finish(vsb1));
	Fh(tl, 0, "%s", VSB_data(vsb1));
	VSB_destroy(&vsb1);

	/* Emit the hosthdr field, fall back to .host if not specified */
	/* If .path is specified, set "0.0.0.0". */
	Fb(tl, 0, "\t.hosthdr = ");
	if (t_hosthdr != NULL)
		EncToken(tl->fb, t_hosthdr);
	else if (t_host != NULL)
		EncToken(tl->fb, t_host);
	else
		Fb(tl, 0, "\"0.0.0.0\"");
	Fb(tl, 0, ",\n");

	/* Close the struct */
	Fb(tl, 0, "};\n");

	vcc_NextToken(tl);

	AZ(VSB_finish(tl->fb));
	Fh(tl, 0, "%s", VSB_data(tl->fb));
	VSB_destroy(&tl->fb);

	ifp = New_IniFin(tl);
	VSB_printf(ifp->ini,
	    "\t%s =\n\t    VRT_new_backend_clustered(ctx, vsc_cluster,\n"
	    "\t\t&vgc_dir_priv_%s);",
	    vgcname, vgcname);
	VSB_printf(ifp->fin, "\t\tVRT_delete_backend(ctx, &%s);", vgcname);
}

/*--------------------------------------------------------------------
 * Parse directors and backends
 */

void
vcc_ParseBackend(struct vcc *tl)
{
	struct token *t_first, *t_be;
	struct symbol *sym;
	const char *dn;

	tl->ndirector++;
	t_first = tl->t;
	SkipToken(tl, ID);		/* ID: backend */

	vcc_ExpectVid(tl, "backend");	/* ID: name */
	ERRCHK(tl);

	t_be = tl->t;
	if (vcc_IdIs(tl->t, "default")) {
		if (tl->first_director != NULL) {
			tl->first_director->noref = 0;
			tl->first_director = NULL;
			tl->default_director = NULL;
		}
		if (tl->default_director != NULL) {
			VSB_cat(tl->sb,
			    "Only one default director possible.\n");
			vcc_ErrWhere(tl, t_first);
			return;
		}
		vcc_NextToken(tl);
		dn = "vgc_backend_default";
		tl->default_director = dn;
	} else {
		sym = VCC_HandleSymbol(tl, BACKEND);
		ERRCHK(tl);
		AN(sym);
		dn = sym->rname;
		if (tl->default_director == NULL) {
			tl->first_director = sym;
			tl->default_director = dn;
			sym->noref = 1;
		}
	}
	Fh(tl, 0, "\nstatic VCL_BACKEND %s;\n", dn);
	vcc_ParseHostDef(tl, t_be, dn);
	if (tl->err) {
		VSB_printf(tl->sb,
		    "\nIn %.*s specification starting at:\n", PF(t_first));
		vcc_ErrWhere(tl, t_first);
		return;
	}
}

void
vcc_Backend_Init(struct vcc *tl)
{
	struct inifin *ifp;

	Fh(tl, 0, "\nstatic struct vsmw_cluster *vsc_cluster;\n");
	ifp = New_IniFin(tl);
	VSB_cat(ifp->ini, "\tvsc_cluster = VRT_VSM_Cluster_New(ctx,\n"
	    "\t    ndirector * VRT_backend_vsm_need(ctx));\n");
	VSB_cat(ifp->ini, "\tif (vsc_cluster == 0)\n\t\treturn(1);");
	VSB_cat(ifp->fin, "\t\tVRT_VSM_Cluster_Destroy(ctx, &vsc_cluster);");
}
