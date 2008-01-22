/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2007 Linpro AS
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

#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>
#include <stdio.h>
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
vcc_EmitBeIdent(struct vsb *v, const struct token *first, const struct token *last)
{

	vsb_printf(v, "\t.ident =");
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
 * The struct vrt_backend_host is emitted to Fh().
 */

static void
vcc_ParseBackendHost(struct tokenlist *tl, int *nbh)
{
	struct token *t_field;
	struct token *t_first;
	struct token *t_host = NULL, *t_fhost = NULL;
	struct token *t_port = NULL, *t_fport = NULL;
	const char *ep;
	struct host *h;

	t_first = tl->t;
	*nbh = tl->nbackend_host++;

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
		vcc_NextToken(tl);
		ExpectErr(tl, ';');
		vcc_NextToken(tl);
		*nbh = h->hnum;
		return;
	}
	ExpectErr(tl, '{');
	vcc_NextToken(tl);

	Fh(tl, 0, "\nstatic const struct vrt_backend_host bh_%d = {\n",
	    *nbh);

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

	while (tl->t->tok == '.') {
		vcc_NextToken(tl);

		ExpectErr(tl, ID);		/* name */
		t_field = tl->t;
		vcc_NextToken(tl);

		ExpectErr(tl, '=');
		vcc_NextToken(tl);

		if (vcc_IdIs(t_field, "host")) {
			ExpectErr(tl, CSTR);
			assert(tl->t->dec != NULL);
			if (t_host != NULL) {
				assert(t_fhost != NULL);
				vsb_printf(tl->sb,
				    "Multiple .host fields in backend: ");
				vcc_ErrToken(tl, t_field);
				vsb_printf(tl->sb, " at\n");
				vcc_ErrWhere(tl, t_fhost);
				vsb_printf(tl->sb, " and\n");
				vcc_ErrWhere(tl, t_field);
				return;
			}
			t_fhost = t_field;
			t_host = tl->t;
			vcc_NextToken(tl);
		} else if (vcc_IdIs(t_field, "port")) {
			ExpectErr(tl, CSTR);
			assert(tl->t->dec != NULL);
			if (t_port != NULL) {
				assert(t_fport != NULL);
				vsb_printf(tl->sb,
				    "Multiple .port fields in backend: ");
				vcc_ErrToken(tl, t_field);
				vsb_printf(tl->sb, " at\n");
				vcc_ErrWhere(tl, t_fport);
				vsb_printf(tl->sb, " and\n");
				vcc_ErrWhere(tl, t_field);
				return;
			}
			t_fport = t_field;
			t_port = tl->t;
			vcc_NextToken(tl);
		} else {
			vsb_printf(tl->sb,
			    "Unknown field in backend host specification: ");
			vcc_ErrToken(tl, t_field);
			vsb_printf(tl->sb, " at\n");
			vcc_ErrWhere(tl, t_field);
			return;
		}

		ExpectErr(tl, ';');
		vcc_NextToken(tl);
	}
	if (t_host == NULL) {
		vsb_printf(tl->sb, "Backend has no hostname\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}

	/* Check that the hostname makes sense */
	ep = CheckHostPort(t_host->dec, "80");
	if (ep != NULL) {
		vsb_printf(tl->sb, "Backend host '%.*s': %s\n", PF(t_host), ep);
		vcc_ErrWhere(tl, t_host);
		return;
	}
	Fh(tl, 0, "\t.hostname = ");
	EncToken(tl->fh, t_host);
	Fh(tl, 0, ",\n");

	/* Check that the hostname makes sense */
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
	}

	ExpectErr(tl, '}');
	vcc_EmitBeIdent(tl->fh, t_first, tl->t);
	Fh(tl, 0, "};\n");
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------
 * Parse a plain backend
 */

void
vcc_ParseBackend(struct tokenlist *tl)
{
	struct token *t_first;
	struct host *h;
	int nbh;

	h = TlAlloc(tl, sizeof *h);
	t_first = tl->t;		/* T_BACKEND */

	vcc_NextToken(tl);

	ExpectErr(tl, ID);		/* name */
	h->name = tl->t;
	vcc_NextToken(tl);

	vcc_ParseBackendHost(tl, &nbh);
	ERRCHK(tl);

	h->hnum = nbh;
	VTAILQ_INSERT_TAIL(&tl->hosts, h, list);

	/* In the compiled vcl we use these macros to refer to backends */
	Fh(tl, 1, "\n#define VGC_backend_%.*s (VCL_conf.backend[%d])\n",
	    PF(h->name), tl->nbackend);

	vcc_AddDef(tl, h->name, R_BACKEND);

	Fi(tl, 0, "\tVRT_init_simple_backend(&VGC_backend_%.*s , &sbe_%.*s);\n",
	    PF(h->name), PF(h->name));
	Ff(tl, 0, "\tVRT_fini_backend(VGC_backend_%.*s);\n", PF(h->name));

	Fc(tl, 0, "\nstatic const struct vrt_simple_backend sbe_%.*s = {\n",
	    PF(h->name));
	Fc(tl, 0, "\t.name = \"%.*s\",\n", PF(h->name));
	Fc(tl, 0, "\t.host = &bh_%d,\n", nbh);
	vcc_EmitBeIdent(tl->fc, t_first, tl->t);
	Fc(tl, 0, "};\n");

	tl->nbackend++;
}

/*--------------------------------------------------------------------
 * Parse directors
 */

static void
vcc_ParseRandomDirector(struct tokenlist *tl, struct token *t_first, struct token *t_dir)
{
	struct token *t_field, *tb, *tw;
	int nbh, nelem;

	(void)t_first;
	(void)t_dir;

	vcc_NextToken(tl);		/* ID: policy (= random) */

	ExpectErr(tl, '{');
	vcc_NextToken(tl);

	Fc(tl, 0,
	    "\nstatic const struct vrt_dir_random_entry vdre_%.*s[] = {\n",
	    PF(t_dir));

	for (nelem = 0; tl->t->tok != '}'; nelem++) {	/* List of members */
		tb = NULL;
		tw = NULL;
		nbh = -1;

		ExpectErr(tl, '{');
		vcc_NextToken(tl);
	
		while (tl->t->tok != '}') {	/* Member fields */
			ExpectErr(tl, '.');
			vcc_NextToken(tl);
			ExpectErr(tl, ID);
			t_field = tl->t;
			vcc_NextToken(tl);
			ExpectErr(tl, '=');
			vcc_NextToken(tl);
			if (vcc_IdIs(t_field, "backend")) {
				assert(tb == NULL);
				tb = t_field;
				vcc_ParseBackendHost(tl, &nbh);
			} else if (vcc_IdIs(t_field, "weight")) {
				assert(tw == NULL);
				ExpectErr(tl, CNUM);
				tw = tl->t;
				vcc_NextToken(tl);
				ExpectErr(tl, ';');
				vcc_NextToken(tl);
			} else {
				ExpectErr(tl, '?');
			}
		}
		assert(tb != NULL);
		Fc(tl, 0, "\t{");
		Fc(tl, 0, ".host = &bh_%d", nbh);
		if (tw != NULL)
			Fc(tl, 0, ", .weight = %.*s", PF(tw));
		Fc(tl, 0, "},\n");
		vcc_NextToken(tl);
	}
	Fc(tl, 0, "\t{ .host = 0 }\n");
	Fc(tl, 0, "}\n");
	Fc(tl, 0,
	    "\nstatic const struct vrt_dir_random vdr_%.*s[] = {\n",
	    PF(t_dir));
	Fc(tl, 0, "\t.nmember = %d,\n", nelem);
	Fc(tl, 0, "\t.members = vdre_%.*s,\n", PF(t_dir));
	vcc_EmitBeIdent(tl->fc, t_first, tl->t);
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------
 * Parse directors
 */

void
vcc_ParseDirector(struct tokenlist *tl)
{
	struct token *t_dir, *t_first;

	vcc_NextToken(tl);		/* ID: director */
	t_first = tl->t;

	ExpectErr(tl, ID);		/* ID: name */
	t_dir = tl->t;
	vcc_NextToken(tl);

	ExpectErr(tl, ID);		/* ID: policy */
	if (vcc_IdIs(tl->t, "random")) 
		vcc_ParseRandomDirector(tl, t_first, t_dir);
	else {
		vsb_printf(tl->sb, "Unknown director policy: ");
		vcc_ErrToken(tl, tl->t);
		vsb_printf(tl->sb, " at\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
}
