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

void
vcc_ParseBackend(struct tokenlist *tl)
{
	unsigned a;
	struct var *vp;
	struct token *t_be = NULL;
	struct token *t_host = NULL;
	struct token *t_port = NULL;
	const char *ep;

	vcc_NextToken(tl);
	ExpectErr(tl, ID);
	t_be = tl->t;
	vcc_AddDef(tl, tl->t, R_BACKEND);
	if (tl->nbackend == 0)
		vcc_AddRef(tl, tl->t, R_BACKEND);
	Fh(tl, 1, "#define VGC_backend_%.*s (VCL_conf.backend[%d])\n",
	    PF(tl->t), tl->nbackend);
	Fc(tl, 0, "\n");
	Fc(tl, 0, "static void\n");
	Fc(tl, 1, "VGC_init_backend_%.*s (void)\n", PF(tl->t));
	Fc(tl, 1, "{\n");
	Fc(tl, 1, "\tstruct backend *backend = VGC_backend_%.*s;\n", PF(tl->t));
	Fc(tl, 1, "\n");
	Fc(tl, 1, "\tVRT_set_backend_name(backend, \"%.*s\");\n", PF(tl->t));
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
		vp = FindVar(tl, tl->t, vcc_be_vars);
		ERRCHK(tl);
		assert(vp != NULL);
		vcc_NextToken(tl);
		ExpectErr(tl, '=');
		vcc_NextToken(tl);
		switch (vp->fmt) {
		case HOSTNAME:
			ExpectErr(tl, CSTR);
			t_host = tl->t;
			Fc(tl, 1, "\t%s ", vp->lname);
			EncToken(tl->fc, t_host);
			Fc(tl, 0, ");\n");
			vcc_NextToken(tl);
			break;
		case PORTNAME:
			ExpectErr(tl, CSTR);
			t_port = tl->t;
			Fc(tl, 1, "\t%s ", vp->lname);
			EncToken(tl->fc, t_port);
			Fc(tl, 0, ");\n");
			vcc_NextToken(tl);
			break;
#if 0
		case INT:
		case SIZE:
		case RATE:
		case FLOAT:
#endif
		case TIME:
			Fc(tl, 1, "\t%s ", vp->lname);
			a = tl->t->tok;
			if (a == T_MUL || a == T_DIV)
				Fc(tl, 0, "%g", vcc_DoubleVal(tl));
			else if (vp->fmt == TIME)
				vcc_TimeVal(tl);
			else if (vp->fmt == SIZE)
				vcc_SizeVal(tl);
			else if (vp->fmt == RATE)
				vcc_RateVal(tl);
			else
				Fc(tl, 0, "%g", vcc_DoubleVal(tl));
			Fc(tl, 0, ");\n");
			break;
		default:
			vsb_printf(tl->sb,
			    "Assignments not possible for '%s'\n", vp->name);
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		ExpectErr(tl, ';');
		vcc_NextToken(tl);
	}
	ExpectErr(tl, '}');
	if (t_host == NULL) {
		vsb_printf(tl->sb, "Backend '%.*s' has no hostname\n",
		    PF(t_be));
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	ep = CheckHostPort(t_host->dec, "80");
	if (ep != NULL) {
		vsb_printf(tl->sb, "Backend '%.*s': %s\n", PF(t_be), ep);
		vcc_ErrWhere(tl, t_host);
		return;
	}
	if (t_port != NULL) {
		ep = CheckHostPort(t_host->dec, t_port->dec);
		if (ep != NULL) {
			vsb_printf(tl->sb,
			    "Backend '%.*s': %s\n", PF(t_be), ep);
			vcc_ErrWhere(tl, t_port);
			return;
		}
	}

	vcc_NextToken(tl);
	Fc(tl, 1, "}\n");
	Fc(tl, 0, "\n");
	Fi(tl, 0, "\tVGC_init_backend_%.*s();\n", PF(t_be));
	Ff(tl, 0, "\tVRT_fini_backend(VGC_backend_%.*s);\n", PF(t_be));
	tl->nbackend++;
}
