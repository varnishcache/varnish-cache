/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
#include <string.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

struct host {
	VTAILQ_ENTRY(host)      list;
	int	                hnum;
	struct token            *name;
};

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
 * Struct sockaddr is not really designed to be a compile time
 * initialized data structure, so we encode it as a byte-string
 * and put it in an official sockaddr when we load the VCL.
 */

static void
Emit_Sockaddr(struct tokenlist *tl, const struct token *t_host,
    const char *port)
{
	struct addrinfo *res, *res0, hint;
	int n4, n6, len, error, retval;
	const char *emit, *multiple;
	unsigned char *u;
	char hbuf[NI_MAXHOST];

	AN(t_host->dec);
	retval = 0;
	memset(&hint, 0, sizeof hint);
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(t_host->dec, port, &hint, &res0);
	AZ(error);
	n4 = n6 = 0;
	multiple = NULL;
	for (res = res0; res; res = res->ai_next) {
		emit = NULL;
		if (res->ai_family == PF_INET) {
			if (n4++ == 0)
				emit = "ipv4_sockaddr";
			else
				multiple = "IPv4";
		} else if (res->ai_family == PF_INET6) {
			if (n6++ == 0)
				emit = "ipv6_sockaddr";
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
			for (res = res0; res != NULL; res = res->ai_next) {
				error = getnameinfo(res->ai_addr,
				    res->ai_addrlen, hbuf, sizeof hbuf,
				    NULL, 0, NI_NUMERICHOST);
				AZ(error);
				vsb_printf(tl->sb, "\t%s\n", hbuf);
			}
			vcc_ErrWhere(tl, t_host);
			return;
		}
		AN(emit);
		AN(res->ai_addr);
		AN(res->ai_addrlen);
		assert(res->ai_addrlen < 256);
		Fh(tl, 0, "\nstatic const unsigned char sockaddr%u[%d] = {\n",
		    tl->nsockaddr, res->ai_addrlen + 1);
		Fh(tl, 0, "    %3u, /* Length */\n",  res->ai_addrlen);
		u = (void*)res->ai_addr;
		for (len = 0; len < res->ai_addrlen; len++) {
			if ((len % 8) == 0)
				Fh(tl, 0, "   ");
			Fh(tl, 0, " %3u", u[len]);
			if (len + 1 < res->ai_addrlen)
				Fh(tl, 0, ",");
			if ((len % 8) == 7)
				Fh(tl, 0, "\n");
		}
		Fh(tl, 0, "\n};\n");
		Fb(tl, 0, "\t.%s = sockaddr%u,\n", emit, tl->nsockaddr++);
		retval++;
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

static void
vcc_EmitBeIdent(struct vsb *v, const struct token *name,
    const struct token *qual, int serial, const struct token *first,
    const struct token *last)
{

	AN(name);
	AN(qual);
	assert(first != last);
	vsb_printf(v, "\t.ident =");
	vsb_printf(v, "\n\t    \"%.*s %.*s [%d] \"",
	    PF(qual), PF(name), serial);
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
 * Helper functions to complain about duplicate and missing fields
 *
 * XXX: idea: add groups to check for exclusivity, such that
 * XXX:    ("!foo", "?bar", "!{", "this", "that", "}", NULL)
 * XXX: means exactly one of "this" or "that", and
 * XXX:    ("!foo", "?bar", "?{", "this", "that", "}", NULL)
 * XXX: means at most one of "this" or "that".
 */

struct fld_spec {
	const char	*name;
	struct token	*found;
};

void
vcc_ResetFldSpec(struct fld_spec *f)
{

	for (; f->name != NULL; f++)
		f->found = NULL;
}

struct fld_spec *
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

void
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

void
vcc_FieldsOk(struct tokenlist *tl, const struct fld_spec *fs)
{

	for (; fs->name != NULL; fs++) {
		if (*fs->name == '!' && fs->found == NULL) {
			vsb_printf(tl->sb,
			    "Mandatory field '%s' missing.\n", fs->name + 1);
			tl->err = 1;
		}
	}
}

/*--------------------------------------------------------------------
 * Parse a backend probe specification
 */

static void
vcc_ProbeRedef(struct tokenlist *tl, struct token **t_did,
    struct token *t_field)
{
	/* .url and .request are mutually exclusive */

	if (*t_did != NULL) {
		vsb_printf(tl->sb,
		    "Probe request redefinition at:\n");
		vcc_ErrWhere(tl, t_field);
		vsb_printf(tl->sb,
		    "Previous definition:\n");
		vcc_ErrWhere(tl, *t_did);
		return;
	}
	*t_did = t_field;
}

static void
vcc_ParseProbe(struct tokenlist *tl)
{
	struct fld_spec *fs;
	struct token *t_field;
	struct token *t_did = NULL, *t_window = NULL, *t_threshold = NULL;
	struct token *t_initial = NULL;
	unsigned window, threshold, initial, status;

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

	ExpectErr(tl, '{');
	vcc_NextToken(tl);

	window = 0;
	threshold = 0;
	initial = 0;
	status = 0;
	Fb(tl, 0, "\t.probe = {\n");
	while (tl->t->tok != '}') {

		vcc_IsField(tl, &t_field, fs);
		ERRCHK(tl);
		if (vcc_IdIs(t_field, "url")) {
			vcc_ProbeRedef(tl, &t_did, t_field);
			ERRCHK(tl);
			ExpectErr(tl, CSTR);
			Fb(tl, 0, "\t\t.url = ");
			EncToken(tl->fb, tl->t);
			Fb(tl, 0, ",\n");
			vcc_NextToken(tl);
		} else if (vcc_IdIs(t_field, "request")) {
			vcc_ProbeRedef(tl, &t_did, t_field);
			ERRCHK(tl);
			ExpectErr(tl, CSTR);
			Fb(tl, 0, "\t\t.request =\n");
			while (tl->t->tok == CSTR) {
				Fb(tl, 0, "\t\t\t");
				EncToken(tl->fb, tl->t);
				Fb(tl, 0, " \"\\r\\n\"\n");
				vcc_NextToken(tl);
			}
			Fb(tl, 0, "\t\t\t\"\\r\\n\",\n");
		} else if (vcc_IdIs(t_field, "timeout")) {
			Fb(tl, 0, "\t\t.timeout = ");
			vcc_TimeVal(tl);
			ERRCHK(tl);
			Fb(tl, 0, ",\n");
		} else if (vcc_IdIs(t_field, "interval")) {
			Fb(tl, 0, "\t\t.interval = ");
			vcc_TimeVal(tl);
			ERRCHK(tl);
			Fb(tl, 0, ",\n");
		} else if (vcc_IdIs(t_field, "window")) {
			t_window = tl->t;
			window = vcc_UintVal(tl);
			vcc_NextToken(tl);
			ERRCHK(tl);
		} else if (vcc_IdIs(t_field, "initial")) {
			t_initial = tl->t;
			initial = vcc_UintVal(tl);
			vcc_NextToken(tl);
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
			vcc_NextToken(tl);
			ERRCHK(tl);
		} else if (vcc_IdIs(t_field, "threshold")) {
			t_threshold = tl->t;
			threshold = vcc_UintVal(tl);
			vcc_NextToken(tl);
			ERRCHK(tl);
		} else {
			vcc_ErrToken(tl, t_field);
			vcc_ErrWhere(tl, t_field);
			ErrInternal(tl);
			return;
		}

		ExpectErr(tl, ';');
		vcc_NextToken(tl);
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
		Fb(tl, 0, "\t\t.window = %u,\n", window);
		Fb(tl, 0, "\t\t.threshold = %u,\n", threshold);
	}
	if (t_initial != NULL)
		Fb(tl, 0, "\t\t.initial = %u,\n", initial);
	else
		Fb(tl, 0, "\t\t.initial = ~0U,\n", initial);
	if (status > 0)
		Fb(tl, 0, "\t\t.exp_status = %u,\n", status);
	Fb(tl, 0, "\t},\n");
	ExpectErr(tl, '}');
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------
 * Parse and emit a backend host definition
 *
 * The struct vrt_backend is emitted to Fh().
 */

static void
vcc_ParseHostDef(struct tokenlist *tl, int *nbh, const struct token *name,
    const struct token *qual, int serial)
{
	struct token *t_field;
	struct token *t_first;
	struct token *t_host = NULL;
	struct token *t_port = NULL;
	struct token *t_hosthdr = NULL;
	unsigned saint = UINT_MAX;
	const char *ep;
	struct fld_spec *fs;
	struct vsb *vsb;
	unsigned u;

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
	t_first = tl->t;

	ExpectErr(tl, '{');
	vcc_NextToken(tl);

	vsb = vsb_newauto();
	AN(vsb);
	tl->fb = vsb;

	*nbh = tl->nbackend_host++;
	Fb(tl, 0, "\nstatic const struct vrt_backend bh_%d = {\n", *nbh);

	Fb(tl, 0, "\t.vcl_name = \"%.*s", PF(name));
	if (serial)
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
			ExpectErr(tl, ';');
			vcc_NextToken(tl);
		} else if (vcc_IdIs(t_field, "port")) {
			ExpectErr(tl, CSTR);
			assert(tl->t->dec != NULL);
			t_port = tl->t;
			vcc_NextToken(tl);
			ExpectErr(tl, ';');
			vcc_NextToken(tl);
		} else if (vcc_IdIs(t_field, "host_header")) {
			ExpectErr(tl, CSTR);
			assert(tl->t->dec != NULL);
			t_hosthdr = tl->t;
			vcc_NextToken(tl);
			ExpectErr(tl, ';');
			vcc_NextToken(tl);
		} else if (vcc_IdIs(t_field, "connect_timeout")) {
			Fb(tl, 0, "\t.connect_timeout = ");
			vcc_TimeVal(tl);
			ERRCHK(tl);
			Fb(tl, 0, ",\n");
			ExpectErr(tl, ';');
			vcc_NextToken(tl);
		} else if (vcc_IdIs(t_field, "first_byte_timeout")) {
			Fb(tl, 0, "\t.first_byte_timeout = ");
			vcc_TimeVal(tl);
			ERRCHK(tl);
			Fb(tl, 0, ",\n");
			ExpectErr(tl, ';');
			vcc_NextToken(tl);
		} else if (vcc_IdIs(t_field, "between_bytes_timeout")) {
			Fb(tl, 0, "\t.between_bytes_timeout = ");
			vcc_TimeVal(tl);
			ERRCHK(tl);
			Fb(tl, 0, ",\n");
			ExpectErr(tl, ';');
			vcc_NextToken(tl);
		} else if (vcc_IdIs(t_field, "max_connections")) {
			u = vcc_UintVal(tl);
			vcc_NextToken(tl);
			ERRCHK(tl);
			ExpectErr(tl, ';');
			vcc_NextToken(tl);
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
			vcc_NextToken(tl);
			ERRCHK(tl);
			saint = u;
			ExpectErr(tl, ';');
			vcc_NextToken(tl);
		} else if (vcc_IdIs(t_field, "probe")) {
			vcc_ParseProbe(tl);
			ERRCHK(tl);
		} else {
			ErrInternal(tl);
			return;
		}

	}

	vcc_FieldsOk(tl, fs);
	ERRCHK(tl);

	/* Check that the hostname makes sense */
	assert(t_host != NULL);
	ep = CheckHostPort(t_host->dec, "80");
	if (ep != NULL) {
		vsb_printf(tl->sb, "Backend host '%.*s': %s\n", PF(t_host), ep);
		vcc_ErrWhere(tl, t_host);
		return;
	}

	/* Check that the portname makes sense */
	if (t_port != NULL) {
		ep = CheckHostPort("127.0.0.1", t_port->dec);
		if (ep != NULL) {
			vsb_printf(tl->sb,
			    "Backend port '%.*s': %s\n", PF(t_port), ep);
			vcc_ErrWhere(tl, t_port);
			return;
		}
		Emit_Sockaddr(tl, t_host, t_port->dec);
	} else {
		Emit_Sockaddr(tl, t_host, "80");
	}
	ERRCHK(tl);

	ExpectErr(tl, '}');

	/* We have parsed it all, emit the ident string */
	vcc_EmitBeIdent(tl->fb, name, qual, serial, t_first, tl->t);

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
	vsb_finish(vsb);
	AZ(vsb_overflowed(vsb));
	Fh(tl, 0, "%s", vsb_data(vsb));
	vsb_delete(vsb);
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
vcc_ParseBackendHost(struct tokenlist *tl, int *nbh, const struct token *name,
    const struct token *qual, int serial)
{
	struct host *h;
	struct token *t;

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
	} else if (tl->t->tok == '{') {
		t = tl->t;
		vcc_ParseHostDef(tl, nbh, name, qual, serial);
		if (tl->err) {
			vsb_printf(tl->sb,
			    "\nIn backend host specification starting at:\n");
			vcc_ErrWhere(tl, t);
		}
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
 * Parse a plain backend aka a simple director
 */

static void
vcc_ParseSimpleDirector(struct tokenlist *tl, const struct token *t_first,
    struct token *t_dir)
{
	struct host *h;

	h = TlAlloc(tl, sizeof *h);
	h->name = t_dir;

	vcc_ParseHostDef(tl, &h->hnum, h->name, t_first, 0);
	ERRCHK(tl);

	VTAILQ_INSERT_TAIL(&tl->hosts, h, list);

	Fi(tl, 0,
	    "\tVRT_init_dir_simple(cli, &VGC_backend_%.*s , &bh_%d);\n",
	    PF(h->name), h->hnum);
	Ff(tl, 0, "\tVRT_fini_dir(cli, VGC_backend_%.*s);\n", PF(h->name));

}

/*--------------------------------------------------------------------
 * Parse directors and backends
 */

static const struct dirlist {
	const char	*name;
	parsedirector_f	*func;
} dirlist[] = {
	{ "random",		vcc_ParseRandomDirector },
	{ "round-robin",	vcc_ParseRoundRobinDirector },
	{ NULL,		NULL }
};

void
vcc_ParseDirector(struct tokenlist *tl)
{
	struct token *t_dir, *t_first, *t_policy;
	struct dirlist const *dl;

	t_first = tl->t;
	vcc_NextToken(tl);		/* ID: director | backend */

	vcc_ExpectCid(tl);		/* ID: name */
	ERRCHK(tl);
	t_dir = tl->t;
	vcc_NextToken(tl);

	Fh(tl, 1, "\n#define VGC_backend_%.*s (VCL_conf.director[%d])\n",
	    PF(t_dir), tl->ndirector);
	vcc_AddDef(tl, t_dir, R_BACKEND);
	tl->ndirector++;

	if (vcc_IdIs(t_first, "backend")) {
		vcc_ParseSimpleDirector(tl, t_first, t_dir);
	} else {
		ExpectErr(tl, ID);		/* ID: policy */
		t_policy = tl->t;
		vcc_NextToken(tl);

		for (dl = dirlist; dl->name != NULL; dl++)
			if (vcc_IdIs(t_policy, dl->name))
				break;
		if (dl->name == NULL) {
			vsb_printf(tl->sb, "Unknown director policy: ");
			vcc_ErrToken(tl, t_policy);
			vsb_printf(tl->sb, " at\n");
			vcc_ErrWhere(tl, t_policy);
			return;
		}
		ExpectErr(tl, '{');
		vcc_NextToken(tl);
		dl->func(tl, t_policy, t_dir);
		if (!tl->err) {
			ExpectErr(tl, '}');
			vcc_NextToken(tl);
		}
	}
	if (tl->err) {
		vsb_printf(tl->sb,
		    "\nIn %.*s specification starting at:\n", PF(t_first));
		vcc_ErrWhere(tl, t_first);
		return;
	}
}
