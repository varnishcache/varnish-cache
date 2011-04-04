/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
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
 * A necessary explanation of a convoluted policy:
 *
 * In VCL we have backends and directors.
 *
 * In VRT we have directors which reference (a number of) backend hosts.
 *
 * A VCL backend therefore has an implicit director of type "simple" created
 * by the compiler, but not visible in VCL.
 *
 * A VCL backend is a "named host", these can be referenced by name from
 * VCL directors, but not from VCL backends.
 *
 * The reason for this quasimadness is that we want to collect statistics
 * for each actual kickable hardware backend machine, but we want to be
 * able to refer to them multiple times in different directors.
 *
 * At the same time, we do not want to force users to declare each backend
 * host with a name, if all they want to do is put it into a director, so
 * backend hosts can be declared inline in the director, in which case
 * its identity is the director and its numerical index therein.
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <sys/types.h>
#include <sys/socket.h>

#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "vsb.h"
#include "vss.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

struct host {
	VTAILQ_ENTRY(host)      list;
	struct token            *name;
	char			*vgcname;
};

static int
emit_sockaddr(struct vcc *tl, void *sa, unsigned sal)
{
	unsigned len;
	uint8_t *u;

	AN(sa);
	AN(sal);
	assert(sal < 256);
	Fh(tl, 0, "\nstatic const unsigned char sockaddr%u[%d] = {\n",
	    tl->nsockaddr, sal + 1);
	Fh(tl, 0, "    %3u, /* Length */\n",  sal);
	u = sa;
	for (len = 0; len <sal; len++) {
		if ((len % 8) == 0)
			Fh(tl, 0, "   ");
		Fh(tl, 0, " %3u", u[len]);
		if (len + 1 < sal)
			Fh(tl, 0, ",");
		if ((len % 8) == 7)
			Fh(tl, 0, "\n");
	}
	Fh(tl, 0, "\n};\n");
	return (tl->nsockaddr++);
}

/*--------------------------------------------------------------------
 * Struct sockaddr is not really designed to be a compile time
 * initialized data structure, so we encode it as a byte-string
 * and put it in an official sockaddr when we load the VCL.
 */

#include <stdio.h>

void
Emit_Sockaddr(struct vcc *tl, const struct token *t_host, const char *port)
{
	struct addrinfo *res, *res0, *res1, hint;
	int n4, n6, error, retval, x;
	const char *emit, *multiple;
	char hbuf[NI_MAXHOST];
	char *hop, *pop;

	AN(t_host->dec);
	retval = 0;
	memset(&hint, 0, sizeof hint);
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;

	if (VSS_parse(t_host->dec, &hop, &pop)) {
		vsb_printf(tl->sb,
		    "Backend host '%.*s': wrong syntax (unbalanced [...] ?)\n",
		    PF(t_host) );
		vcc_ErrWhere(tl, t_host);
		return;
	}
	error = getaddrinfo(
	    hop != NULL ? hop : t_host->dec,
	    pop != NULL ? pop : port,
	    &hint, &res0);
	free(hop);
	free(pop);
	if (error) {
		vsb_printf(tl->sb,
		    "Backend host '%.*s'"
		    " could not be resolved to an IP address:\n", PF(t_host));
		vsb_printf(tl->sb,
		    "\t%s\n"
		    "(Sorry if that error message is gibberish.)\n",
		    gai_strerror(error));
		vcc_ErrWhere(tl, t_host);
		return;
	}
	AZ(error);
	n4 = n6 = 0;
	multiple = NULL;

	for (res = res0; res; res = res->ai_next) {
		emit = NULL;
		if (res->ai_family == PF_INET) {
			if (n4++ == 0)
				emit = "ipv4";
			else
				multiple = "IPv4";
		} else if (res->ai_family == PF_INET6) {
			if (n6++ == 0)
				emit = "ipv6";
			else
				multiple = "IPv6";
		} else
			continue;

		if (multiple != NULL) {
			vsb_printf(tl->sb,
			    "Backend host %.*s: resolves to "
			    "multiple %s addresses.\n"
			    "Only one address is allowed.\n"
			    "Please specify which exact address "
			    "you want to use, we found these:\n",
			    PF(t_host), multiple);
			for (res1 = res0; res1 != NULL; res1 = res1->ai_next) {
				error = getnameinfo(res1->ai_addr,
				    res1->ai_addrlen, hbuf, sizeof hbuf,
				    NULL, 0, NI_NUMERICHOST);
				AZ(error);
				vsb_printf(tl->sb, "\t%s\n", hbuf);
			}
			vcc_ErrWhere(tl, t_host);
			return;
		}
		AN(emit);
		x = emit_sockaddr(tl, res->ai_addr, res->ai_addrlen);
		Fb(tl, 0, "\t.%s_sockaddr = sockaddr%u,\n", emit, x);
		error = getnameinfo(res->ai_addr,
		    res->ai_addrlen, hbuf, sizeof hbuf,
		    NULL, 0, NI_NUMERICHOST);
		AZ(error);
		Fb(tl, 0, "\t.%s_addr = \"%s\",\n", emit, hbuf);
		retval++;
	}
	if (res0 != NULL) {
		error = getnameinfo(res0->ai_addr,
		    res0->ai_addrlen, NULL, 0, hbuf, sizeof hbuf,
		    NI_NUMERICSERV);
		AZ(error);
		Fb(tl, 0, "\t.port = \"%s\",\n", hbuf);
	}
	freeaddrinfo(res0);
	if (retval == 0) {
		vsb_printf(tl->sb,
		    "Backend host '%.*s': resolves to "
		    "neither IPv4 nor IPv6 addresses.\n",
		    PF(t_host) );
		vcc_ErrWhere(tl, t_host);
	}
}

/*--------------------------------------------------------------------
 * When a new VCL is loaded, it is likely to contain backend declarations
 * identical to other loaded VCL programs, and we want to reuse the state
 * of those in order to not have to relearn statistics, DNS etc.
 *
 * This function emits a space separated text-string of the tokens which
 * define a given backend which can be used to determine "identical backend"
 * in that context.
 */

void
vcc_EmitBeIdent(const struct vcc *tl, struct vsb *v,
    int serial, const struct token *first, const struct token *last)
{

	assert(first != last);
	vsb_printf(v, "\t.ident =");
	if (serial >= 0) {
		vsb_printf(v, "\n\t    \"%.*s %.*s [%d] \"",
		    PF(tl->t_policy), PF(tl->t_dir), serial);
	} else {
		vsb_printf(v, "\n\t    \"%.*s %.*s \"",
		    PF(tl->t_policy), PF(tl->t_dir));
	}
	while (1) {
		if (first->dec != NULL)
			vsb_printf(v, "\n\t    \"\\\"\" %.*s \"\\\" \"",
			    PF(first));
		else
			vsb_printf(v, "\n\t    \"%.*s \"", PF(first));
		if (first == last)
			break;
		first = VTAILQ_NEXT(first, list);
		AN(first);
	}
	vsb_printf(v, ",\n");
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
		vsb_printf(tl->sb, "Probe request redefinition at:\n");
		vcc_ErrWhere(tl, t_field);
		vsb_printf(tl->sb, "Previous definition:\n");
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
			vcc_TimeVal(tl, &t);
			ERRCHK(tl);
			Fh(tl, 0, "%g,\n", t);
		} else if (vcc_IdIs(t_field, "interval")) {
			Fh(tl, 0, "\t.interval = ");
			vcc_TimeVal(tl, &t);
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
				vsb_printf(tl->sb,
				    "Must specify .status with exactly three "
				    " digits (100 <= x <= 999)\n");
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
			vsb_printf(tl->sb,
			    "Must specify .threshold with .window\n");
			vcc_ErrWhere(tl, t_window);
			return;
		} else if (t_threshold != NULL && t_window == NULL) {
			if (threshold > 64) {
				vsb_printf(tl->sb,
				    "Threshold must be 64 or less.\n");
				vcc_ErrWhere(tl, t_threshold);
				return;
			}
			window = threshold + 1;
		} else if (window > 64) {
			AN(t_window);
			vsb_printf(tl->sb, "Window must be 64 or less.\n");
			vcc_ErrWhere(tl, t_window);
			return;
		}
		if (threshold > window ) {
			vsb_printf(tl->sb,
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
		Fh(tl, 0, "\t.initial = ~0U,\n", initial);
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

	vcc_NextToken(tl);		/* ID: probe */

	vcc_ExpectCid(tl);		/* ID: name */
	ERRCHK(tl);
	t_probe = tl->t;
	vcc_NextToken(tl);
	vcc_AddDef(tl, t_probe, SYM_PROBE);

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
vcc_ParseHostDef(struct vcc *tl, int serial, const char *vgcname)
{
	struct token *t_field;
	struct token *t_host = NULL;
	struct token *t_port = NULL;
	struct token *t_hosthdr = NULL;
	unsigned saint = UINT_MAX;
	struct fld_spec *fs;
	struct vsb *vsb;
	unsigned u;
	double t;

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
	    "?saintmode_threshold",
	    NULL);

	SkipToken(tl, '{');

	vsb = vsb_new_auto();
	AN(vsb);
	tl->fb = vsb;

	Fb(tl, 0, "\nstatic const struct vrt_backend vgc_dir_priv_%s = {\n",
	    vgcname);

	Fb(tl, 0, "\t.vcl_name = \"%.*s", PF(tl->t_dir));
	if (serial >= 0)
		Fb(tl, 0, "[%d]", serial);
	Fb(tl, 0, "\",\n");

	/* Check for old syntax */
	if (tl->t->tok == ID && vcc_IdIs(tl->t, "set")) {
		vsb_printf(tl->sb,
		    "NB: Backend Syntax has changed:\n"
		    "Remove \"set\" and \"backend\" in front"
		    " of backend fields.\n" );
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb, " at ");
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
			vcc_TimeVal(tl, &t);
			ERRCHK(tl);
			Fb(tl, 0, "%g,\n", t);
			SkipToken(tl, ';');
		} else if (vcc_IdIs(t_field, "first_byte_timeout")) {
			Fb(tl, 0, "\t.first_byte_timeout = ");
			vcc_TimeVal(tl, &t);
			ERRCHK(tl);
			Fb(tl, 0, "%g,\n", t);
			SkipToken(tl, ';');
		} else if (vcc_IdIs(t_field, "between_bytes_timeout")) {
			Fb(tl, 0, "\t.between_bytes_timeout = ");
			vcc_TimeVal(tl, &t);
			ERRCHK(tl);
			Fb(tl, 0, "%g,\n", t);
			SkipToken(tl, ';');
		} else if (vcc_IdIs(t_field, "max_connections")) {
			u = vcc_UintVal(tl);
			ERRCHK(tl);
			SkipToken(tl, ';');
			Fb(tl, 0, "\t.max_connections = %u,\n", u);
		} else if (vcc_IdIs(t_field, "saintmode_threshold")) {
			u = vcc_UintVal(tl);
			/* UINT_MAX == magic number to mark as unset, so
			 * not allowed here.
			 */
			if (u == UINT_MAX) {
				vsb_printf(tl->sb,
				    "Value outside allowed range: ");
				vcc_ErrToken(tl, tl->t);
				vsb_printf(tl->sb, " at\n");
				vcc_ErrWhere(tl, tl->t);
			}
			ERRCHK(tl);
			saint = u;
			SkipToken(tl, ';');
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
			vsb_printf(tl->sb,
			    "Expected '{' or name of probe.");
			vcc_ErrToken(tl, tl->t);
			vsb_printf(tl->sb, " at\n");
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

	Fb(tl, 0, "\t.saintmode_threshold = %d,\n",saint);

	/* Close the struct */
	Fb(tl, 0, "};\n");

	vcc_NextToken(tl);

	tl->fb = NULL;
	AZ(vsb_finish(vsb));
	Fh(tl, 0, "%s", vsb_data(vsb));
	vsb_delete(vsb);

	Fi(tl, 0, "\tVRT_init_dir(cli, VCL_conf.director, \"simple\",\n"
	    "\t    VGC_backend_%s, &vgc_dir_priv_%s);\n", vgcname, vgcname);
	Ff(tl, 0, "\tVRT_fini_dir(cli, VGCDIR(%s));\n", vgcname);
	tl->ndirector++;
}

/*--------------------------------------------------------------------
 * Parse and emit a backend host specification.
 *
 * The syntax is the following:
 *
 * backend_spec:
 *	name_of_backend		# by reference
 *	'{' be_elements '}'	# by specification
 *
 * The struct vrt_backend is emitted to Fh().
 */

void
vcc_ParseBackendHost(struct vcc *tl, int serial, char **nm)
{
	struct host *h;
	struct token *t;
	char vgcname[BUFSIZ];

	AN(nm);
	*nm = NULL;
	if (tl->t->tok == ID) {
		VTAILQ_FOREACH(h, &tl->hosts, list) {
			if (vcc_Teq(h->name, tl->t))
				break;
		}
		if (h == NULL) {
			vsb_printf(tl->sb, "Reference to unknown backend ");
			vcc_ErrToken(tl, tl->t);
			vsb_printf(tl->sb, " at\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		vcc_AddRef(tl, h->name, SYM_BACKEND);
		vcc_NextToken(tl);
		SkipToken(tl, ';');
		*nm = h->vgcname;
	} else if (tl->t->tok == '{') {
		t = tl->t;

		sprintf(vgcname, "%.*s_%d", PF(tl->t_dir), serial);

		vcc_ParseHostDef(tl, serial, vgcname);
		if (tl->err) {
			vsb_printf(tl->sb,
			    "\nIn backend host specification starting at:\n");
			vcc_ErrWhere(tl, t);
		}
		*nm = strdup(vgcname);	 /* XXX */

		return;
	} else {
		vsb_printf(tl->sb,
		    "Expected a backend host specification here, "
		    "either by name or by {...}\n");
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb, " at\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
}

/*--------------------------------------------------------------------
 * Tell rest of compiler about a backend
 */

static void
vcc_DefBackend(struct vcc *tl, const struct token *nm)
{
	struct symbol *sym;

	sym = VCC_GetSymbolTok(tl, nm, SYM_BACKEND);
	AN(sym);
	sym->fmt = BACKEND;
	sym->eval = vcc_Eval_Backend;
	sym->ndef++;
}

/*--------------------------------------------------------------------
 * Parse a plain backend aka a simple director
 */

static void
vcc_ParseSimpleDirector(struct vcc *tl)
{
	struct host *h;
	char vgcname[BUFSIZ];

	h = TlAlloc(tl, sizeof *h);
	h->name = tl->t_dir;
	vcc_DefBackend(tl, tl->t_dir);
	sprintf(vgcname, "_%.*s", PF(h->name));
	h->vgcname = TlAlloc(tl, strlen(vgcname) + 1);
	strcpy(h->vgcname, vgcname);

	vcc_ParseHostDef(tl, -1, vgcname);
	ERRCHK(tl);

	VTAILQ_INSERT_TAIL(&tl->hosts, h, list);
}

/*--------------------------------------------------------------------
 * Parse directors and backends
 */

static const struct dirlist {
	const char	*name;
	parsedirector_f	*func;
} dirlist[] = {
	{ "hash",		vcc_ParseRandomDirector },
	{ "random",		vcc_ParseRandomDirector },
	{ "client",		vcc_ParseRandomDirector },
	{ "round-robin",	vcc_ParseRoundRobinDirector },
	{ "dns",		vcc_ParseDnsDirector },
	{ NULL,		NULL }
};

void
vcc_ParseDirector(struct vcc *tl)
{
	struct token *t_first;
	struct dirlist const *dl;
	int isfirst;

	t_first = tl->t;
	vcc_NextToken(tl);		/* ID: director | backend */

	vcc_ExpectCid(tl);		/* ID: name */
	ERRCHK(tl);
	tl->t_dir = tl->t;
	vcc_NextToken(tl);


	isfirst = tl->ndirector;
	if (vcc_IdIs(t_first, "backend")) {
		tl->t_policy = t_first;
		vcc_ParseSimpleDirector(tl);
	} else {
		vcc_DefBackend(tl, tl->t_dir);
		ExpectErr(tl, ID);		/* ID: policy */
		tl->t_policy = tl->t;
		vcc_NextToken(tl);

		for (dl = dirlist; dl->name != NULL; dl++)
			if (vcc_IdIs(tl->t_policy, dl->name))
				break;
		if (dl->name == NULL) {
			vsb_printf(tl->sb, "Unknown director policy: ");
			vcc_ErrToken(tl, tl->t_policy);
			vsb_printf(tl->sb, " at\n");
			vcc_ErrWhere(tl, tl->t_policy);
			return;
		}
		Ff(tl, 0, "\tVRT_fini_dir(cli, VGCDIR(_%.*s));\n",
		    PF(tl->t_dir));
		SkipToken(tl, '{');
		dl->func(tl);
		if (!tl->err)
			SkipToken(tl, '}');
		Fh(tl, 1, "\n#define VGC_backend__%.*s %d\n",
		    PF(tl->t_dir), tl->ndirector);
		tl->ndirector++;
		Fi(tl, 0,
		    "\tVRT_init_dir(cli, VCL_conf.director, \"%.*s\",\n",
		    PF(tl->t_policy));
		Fi(tl, 0, "\t    VGC_backend__%.*s, &vgc_dir_priv_%.*s);\n",
		    PF(tl->t_dir), PF(tl->t_dir));


	}
	if (tl->err) {
		vsb_printf(tl->sb,
		    "\nIn %.*s specification starting at:\n", PF(t_first));
		vcc_ErrWhere(tl, t_first);
		return;
	}

	if (isfirst == 1 || vcc_IdIs(tl->t_dir, "default")) {
		tl->defaultdir = tl->ndirector - 1;
		tl->t_defaultdir = tl->t_dir;
	}

	tl->t_policy = NULL;
	tl->t_dir = NULL;
}
