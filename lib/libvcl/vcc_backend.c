/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * $Id$
 */

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

static const char *
CheckHostPort(const char *host, const char *port)
{
	struct addrinfo *res, hint;
	int error;

	memset(&hint, 0, sizeof hint);
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(host, port, &hint, &res);
	if (error)
		return (gai_strerror(error));
	freeaddrinfo(res);
	return (NULL);
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

static void
vcc_EmitBeIdent(struct vsb *v, const struct token *name, const char *qual, int serial, const struct token *first, const struct token *last)
{

	vsb_printf(v, "\t.ident =");
	AN(qual);
	vsb_printf(v, "\n\t    \"%s %.*s\"", qual, PF(name));
	if (serial != 0)
		vsb_printf(v, "\n\t    \"[%d]\"", serial);
	while (first != last) {
		if (first->dec != NULL)
			vsb_printf(v, "\n\t    \"\\\"\" %.*s \"\\\" \"",
			    PF(first));
		else
			vsb_printf(v, "\n\t    \"%.*s \"", PF(first));
		first = VTAILQ_NEXT(first, list);
	}
	vsb_printf(v, ",\n");
}

/*--------------------------------------------------------------------
 * Helper functions to complain about duplicate and missing fields
 */

struct fld_spec {
	const char	*name;
	struct token	*found;
};

static void
vcc_ResetFldSpec(struct fld_spec *f)
{

	for (; f->name != NULL; f++)
		f->found = NULL;
}

static struct fld_spec *
vcc_FldSpec(struct tokenlist *tl, const char *first, ...)
{
	struct fld_spec f[100], *r;
	int n = 0;
	va_list ap;
	const char *p;

	f[n++].name = first;
	va_start(ap, first);
	while (1) {
		p = va_arg(ap, const char *);
		if (p == NULL)
			break;
		f[n++].name = p;
		assert(n < 100);
	}
	va_end(ap);
	f[n++].name = NULL;

	vcc_ResetFldSpec(f);

	r = TlAlloc(tl, sizeof *r * n);
	memcpy(r, f, n * sizeof *r);
	return (r);
}


static void
vcc_IsField(struct tokenlist *tl, struct token **t, struct fld_spec *fs)
{
	struct token *t_field;

	ExpectErr(tl, '.');
	vcc_NextToken(tl);
	ExpectErr(tl, ID);
	t_field = tl->t;
	*t = t_field;
	vcc_NextToken(tl);
	ExpectErr(tl, '=');
	vcc_NextToken(tl);

	for (; fs->name != NULL; fs++) {
		if (!vcc_IdIs(t_field, fs->name + 1)) 
			continue;
		if (fs->found == NULL) {
			fs->found = t_field;
			return;
		}
		vsb_printf(tl->sb, "Field ");
		vcc_ErrToken(tl, t_field);
		vsb_printf(tl->sb, " redefined at:\n");
		vcc_ErrWhere(tl, t_field);
		vsb_printf(tl->sb, "\nFirst defined at:\n");
		vcc_ErrWhere(tl, fs->found);
		return;
	}
	vsb_printf(tl->sb, "Unknown field: ");
	vcc_ErrToken(tl, t_field);
	vsb_printf(tl->sb, " at\n");
	vcc_ErrWhere(tl, t_field);
	return;
}

static void
vcc_FieldsOk(struct tokenlist *tl, const struct fld_spec *fs)
{
	int ok = 1;

	for (; fs->name != NULL; fs++) {
		if (*fs->name == '!' && fs->found == NULL) {
			vsb_printf(tl->sb,
			    "Mandatory field .'%s' missing.\n", fs->name + 1);
			ok = 0;
		}
	}
	if (!ok) {
		vcc_ErrWhere(tl, tl->t);
	}
	return;
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
 * be_elements:
 *	be_element
 *	be_element be_elements
 *
 * be_element:
 *	'.' name '=' value ';'
 *
 * The struct vrt_backend is emitted to Fh().
 */

static void
vcc_ParseBackendHost(struct tokenlist *tl, int *nbh, const struct token *name, const char *qual, int serial)
{
	struct token *t_field;
	struct token *t_first;
	struct token *t_host = NULL;
	struct token *t_port = NULL;
	const char *ep;
	struct host *h;
	struct fld_spec *fs;

	fs = vcc_FldSpec(tl, "!host", "?port", "?connect_timeout", NULL);
	t_first = tl->t;

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
		vcc_AddRef(tl, h->name, R_BACKEND);
		vcc_NextToken(tl);
		ExpectErr(tl, ';');
		vcc_NextToken(tl);
		*nbh = h->hnum;
		return;
	}
	ExpectErr(tl, '{');
	vcc_NextToken(tl);

	*nbh = tl->nbackend_host++;
	Fh(tl, 0, "\nstatic const struct vrt_backend bh_%d = {\n", *nbh);

	/* Check for old syntax */
	if (tl->t->tok == ID && vcc_IdIs(tl->t, "set")) {
		vsb_printf(tl->sb,
		    "NB: Backend Syntax has changed:\n"
		    "Remove \"set\" and \"backend\" in front"
		    " of backend fields.\n" );
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb, " at\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}

	while (tl->t->tok != '}') {

		vcc_IsField(tl, &t_field, fs);
		if (tl->err)
			break;
		if (vcc_IdIs(t_field, "host")) {
			ExpectErr(tl, CSTR);
			assert(tl->t->dec != NULL);
			t_host = tl->t;
			vcc_NextToken(tl);
		} else if (vcc_IdIs(t_field, "port")) {
			ExpectErr(tl, CSTR);
			assert(tl->t->dec != NULL);
			t_port = tl->t;
			vcc_NextToken(tl);
		} else if (vcc_IdIs(t_field, "connect_timeout")) {
			Fh(tl, 0, "\t.connect_timeout = ");
			tl->fb = tl->fh;
			vcc_TimeVal(tl);
			tl->fb = NULL;
			ERRCHK(tl);
			Fh(tl, 0, ",\n");
		} else {
			ErrInternal(tl);
			return;
		}

		ExpectErr(tl, ';');
		vcc_NextToken(tl);
	}
	if (!tl->err)
		vcc_FieldsOk(tl, fs);
	if (tl->err) {
		vsb_printf(tl->sb,
		    "\nIn backend host specfication starting at:\n");
		vcc_ErrWhere(tl, t_first);
		return;
	}

	/* Check that the hostname makes sense */
	assert(t_host != NULL);
	ep = CheckHostPort(t_host->dec, "80");
	if (ep != NULL) {
		vsb_printf(tl->sb, "Backend host '%.*s': %s\n", PF(t_host), ep);
		vcc_ErrWhere(tl, t_host);
		return;
	}
	Fh(tl, 0, "\t.hostname = ");
	EncToken(tl->fh, t_host);
	Fh(tl, 0, ",\n");

	/* Check that the portname makes sense */
	if (t_port != NULL) {
		ep = CheckHostPort(t_host->dec, t_port->dec);
		if (ep != NULL) {
			vsb_printf(tl->sb,
			    "Backend port '%.*s': %s\n", PF(t_port), ep);
			vcc_ErrWhere(tl, t_port);
			return;
		}
		Fh(tl, 0, "\t.portname = ");
		EncToken(tl->fh, t_port);
		Fh(tl, 0, ",\n");
	} else {
		Fh(tl, 0, "\t.portname = \"80\",\n");
	}

	ExpectErr(tl, '}');
	vcc_EmitBeIdent(tl->fh, name, qual, serial, t_first, tl->t);
	Fh(tl, 0, "\t.vcl_name = \"%.*s", PF(name));
	if (serial)
		Fh(tl, 0, "[%d]", serial);
	Fh(tl, 0, "\"\n};\n");
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------
 * Parse a plain backend
 */

void
vcc_ParseBackend(struct tokenlist *tl)
{
	struct host *h;
	int nbh;

	h = TlAlloc(tl, sizeof *h);

	vcc_NextToken(tl);

	ExpectErr(tl, ID);		/* name */
	h->name = tl->t;
	vcc_NextToken(tl);

	vcc_ParseBackendHost(tl, &nbh, h->name, "backend", 0);
	ERRCHK(tl);

	h->hnum = nbh;
	VTAILQ_INSERT_TAIL(&tl->hosts, h, list);

	/* In the compiled vcl we use these macros to refer to backends */
	Fh(tl, 1, "\n#define VGC_backend_%.*s (VCL_conf.director[%d])\n",
	    PF(h->name), tl->nbackend);

	vcc_AddDef(tl, h->name, R_BACKEND);

	Fi(tl, 0,
	    "\tVRT_init_dir_simple(cli, &VGC_backend_%.*s , &sbe_%.*s);\n",
	    PF(h->name), PF(h->name));
	Ff(tl, 0, "\tVRT_fini_dir(cli, VGC_backend_%.*s);\n", PF(h->name));

	Fc(tl, 0, "\nstatic const struct vrt_dir_simple sbe_%.*s = {\n",
	    PF(h->name));
	Fc(tl, 0, "\t.name = \"%.*s\",\n", PF(h->name));
	Fc(tl, 0, "\t.host = &bh_%d,\n", nbh);
	Fc(tl, 0, "};\n");

	tl->nbackend++;
}

/*--------------------------------------------------------------------
 * Parse directors
 */

static void
vcc_ParseRandomDirector(struct tokenlist *tl, struct token *t_dir)
{
	struct token *t_field;
	int nbh, nelem;
	struct fld_spec *fs;
	unsigned u;

	Fh(tl, 1, "\n#define VGC_backend_%.*s (VCL_conf.director[%d])\n",
	    PF(t_dir), tl->nbackend);
	vcc_AddDef(tl, t_dir, R_BACKEND);

	fs = vcc_FldSpec(tl, "!backend", "!weight", NULL);

	vcc_NextToken(tl);		/* ID: policy (= random) */

	ExpectErr(tl, '{');
	vcc_NextToken(tl);

	Fc(tl, 0,
	    "\nstatic const struct vrt_dir_random_entry vdre_%.*s[] = {\n",
	    PF(t_dir));

	for (nelem = 0; tl->t->tok != '}'; nelem++) {	/* List of members */
		vcc_ResetFldSpec(fs);
		nbh = -1;

		ExpectErr(tl, '{');
		vcc_NextToken(tl);
		Fc(tl, 0, "\t{");
	
		while (tl->t->tok != '}') {	/* Member fields */
			vcc_IsField(tl, &t_field, fs);
			ERRCHK(tl);
			if (vcc_IdIs(t_field, "backend")) {
				vcc_ParseBackendHost(tl, &nbh,
				    t_dir, "random", nelem);
				Fc(tl, 0, " .host = &bh_%d,", nbh);
				ERRCHK(tl);
			} else if (vcc_IdIs(t_field, "weight")) {
				ExpectErr(tl, CNUM);
				u = vcc_UintVal(tl);
				if (u == 0) {
					vsb_printf(tl->sb,
					    "The .weight must be higher than zero.");
					vcc_ErrToken(tl, tl->t);
					vsb_printf(tl->sb, " at\n");
					vcc_ErrWhere(tl, tl->t);
					return;
				}
				Fc(tl, 0, " .weight = %u", u);
				vcc_NextToken(tl);
				ExpectErr(tl, ';');
				vcc_NextToken(tl);
			} else {
				ErrInternal(tl);
			}
		}
		vcc_FieldsOk(tl, fs);
		ERRCHK(tl);
		Fc(tl, 0, " },\n");
		vcc_NextToken(tl);
	}
	Fc(tl, 0, "\t{ .host = 0 }\n");
	Fc(tl, 0, "};\n");
	Fc(tl, 0,
	    "\nstatic const struct vrt_dir_random vdr_%.*s = {\n",
	    PF(t_dir));
	Fc(tl, 0, "\t.name = \"%.*s\",\n", PF(t_dir));
	Fc(tl, 0, "\t.nmember = %d,\n", nelem);
	Fc(tl, 0, "\t.members = vdre_%.*s,\n", PF(t_dir));
	Fc(tl, 0, "};\n");
	vcc_NextToken(tl);
	Fi(tl, 0,
	    "\tVRT_init_dir_random(cli, &VGC_backend_%.*s , &vdr_%.*s);\n",
	    PF(t_dir), PF(t_dir));
	Ff(tl, 0, "\tVRT_fini_dir(cli, VGC_backend_%.*s);\n", PF(t_dir));
}

/*--------------------------------------------------------------------
 * Parse directors
 */

void
vcc_ParseDirector(struct tokenlist *tl)
{
	struct token *t_dir, *t_first;

	t_first = tl->t;
	vcc_NextToken(tl);		/* ID: director */

	ExpectErr(tl, ID);		/* ID: name */
	t_dir = tl->t;
	vcc_NextToken(tl);

	ExpectErr(tl, ID);		/* ID: policy */
	if (vcc_IdIs(tl->t, "random")) 
		vcc_ParseRandomDirector(tl, t_dir);
	else {
		vsb_printf(tl->sb, "Unknown director policy: ");
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb, " at\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	if (tl->err) {
		vsb_printf(tl->sb,
		    "\nIn director specfication starting at:\n",
		    PF(t_first));
		vcc_ErrWhere(tl, t_first);
		return;
	}
	tl->nbackend++;
}
