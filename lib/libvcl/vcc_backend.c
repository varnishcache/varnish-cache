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
 * Parse and emit a backend specification.
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

void
vcc_ParseSimpleBackend(struct tokenlist *tl)
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

void
vcc_ParseBalancedBackend(struct tokenlist *tl)
{
	struct var *vp;
	struct token *t_be = NULL;
	struct token *t_host = NULL;
	struct token *t_port = NULL;
	double t_weight = 0;
	const char *ep;
	int cnt = 0;
	int weighted = 0;
	double weight = 0;
	unsigned backend_type = tl->t->tok;

	vcc_NextToken(tl);
	ExpectErr(tl, ID);
	t_be = tl->t;
	vcc_AddDef(tl, tl->t, R_BACKEND);

	/* In the compiled vcl we use these macros to refer to backends */
	Fh(tl, 1, "#define VGC_backend_%.*s (VCL_conf.backend[%d])\n",
	    PF(tl->t), tl->nbackend);

	vcc_NextToken(tl);
	ExpectErr(tl, '{');
	vcc_NextToken(tl);
	
	while (1) {
		if (tl->t->tok == '}')
			break;
		ExpectErr(tl, ID);
		if (!vcc_IdIs(tl->t, "set")) {
			vsb_printf(tl->sb,
			    "Expected 'set', found ");
			vcc_ErrToken(tl, tl->t);
			vsb_printf(tl->sb, " at\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		vcc_NextToken(tl);
		ExpectErr(tl, VAR);
		vp = vcc_FindVar(tl, tl->t, vcc_be_vars);
		ERRCHK(tl);
		assert(vp != NULL);
		vcc_NextToken(tl);
		ExpectErr(tl, '=');
		vcc_NextToken(tl);
		if (vp->fmt != SET) {
			vsb_printf(tl->sb,
			    "Assignments not possible for '%s'\n", vp->name);
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		
		ExpectErr(tl, '{');
		vcc_NextToken(tl);
		
		while (1) {
			if (tl->t->tok == '}')
				break;
				
			ExpectErr(tl, '{');
			vcc_NextToken(tl);
			
			// Host
			ExpectErr(tl, CSTR);
			t_host = tl->t;
			vcc_NextToken(tl);
		
			ep = CheckHostPort(t_host->dec, "80");
			if (ep != NULL) {
				vsb_printf(tl->sb, "Backend '%.*s': %s\n", PF(t_be), ep);
				vcc_ErrWhere(tl, t_host);
				return;
			}
			
			if (tl->t->tok == ',') {
				vcc_NextToken(tl);
				
				// Port
				
				ExpectErr(tl, CSTR);
				t_port = tl->t;
				vcc_NextToken(tl);
				
				ep = CheckHostPort(t_host->dec, t_port->dec);
				if (ep != NULL) {
					vsb_printf(tl->sb,
					    "Backend '%.*s': %s\n", PF(t_be), ep);
					vcc_ErrWhere(tl, t_port);
					return;
				}
				
				if (tl->t->tok == ',') {
				
					vcc_NextToken(tl);
					
					// Weight
					t_weight = vcc_DoubleVal(tl);
					weighted = 1;
					weight += t_weight;
				}
			}
						
			ExpectErr(tl, '}');
			vcc_NextToken(tl);
		
			Fc(tl, 0, "\nstatic struct vrt_backend_entry bentry_%.*s_%d = {\n",
				PF(t_be), cnt);
			Fc(tl, 0, "\t.port = %.*s,\n", PF(t_port));
			Fc(tl, 0, "\t.host = %.*s,\n", PF(t_host));
			Fc(tl, 0, "\t.weight = %f,\n", t_weight);
			if (cnt > 0) {
				Fc(tl, 0, "\t.next = &bentry_%.*s_%d\n", PF(t_be), cnt-1);
			} /*else {
				Fc(tl, 0, "\t.next = NULL\n");
			}*/
			Fc(tl, 0, "};\n");
			t_weight = 0;
			cnt++;
		}
		ExpectErr(tl, '}');
		vcc_NextToken(tl);
		ExpectErr(tl, ';');
		vcc_NextToken(tl);
		
		if (t_host == NULL) {
			vsb_printf(tl->sb, "Backend '%.*s' has no hostname\n",
			PF(t_be));
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		
		if (weighted && (int)weight != 1) {
			vsb_printf(tl->sb, "Total weight must be 1\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		
		if (backend_type == T_BACKEND_ROUND_ROBIN) {
			Fc(tl, 0, "\nstatic struct vrt_round_robin_backend sbe_%.*s = {\n",
			    PF(t_be));
			Fc(tl, 0, "\t.name = \"%.*s\",\n", PF(t_be));
			Fc(tl, 0, "\t.count = %d,\n", cnt);
			Fc(tl, 0, "\t.bentry = &bentry_%.*s_%d\n", PF(t_be), cnt-1);
			Fc(tl, 0, "};\n");
			Fi(tl, 0, "\tVRT_init_round_robin_backend(&VGC_backend_%.*s , &sbe_%.*s);\n",
			    PF(t_be), PF(t_be));
		} else if (backend_type == T_BACKEND_RANDOM) {
			Fc(tl, 0, "\nstatic struct vrt_random_backend sbe_%.*s = {\n",
			    PF(t_be));
			Fc(tl, 0, "\t.name = \"%.*s\",\n", PF(t_be));
			Fc(tl, 0, "\t.weighted = %d,\n", weighted);
			Fc(tl, 0, "\t.count = %d,\n", cnt);
			Fc(tl, 0, "\t.bentry = &bentry_%.*s_%d\n", PF(t_be), cnt-1);
			Fc(tl, 0, "};\n");
			Fi(tl, 0, "\tVRT_init_random_backend(&VGC_backend_%.*s , &sbe_%.*s);\n",
			    PF(t_be), PF(t_be));
		}
		Ff(tl, 0, "\tVRT_fini_backend(VGC_backend_%.*s);\n", PF(t_be));

	}
	ExpectErr(tl, '}');

	vcc_NextToken(tl);
	tl->nbackend++;
}

