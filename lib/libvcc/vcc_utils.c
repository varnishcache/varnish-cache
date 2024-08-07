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

#include "config.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "vcc_compile.h"

#include "vre.h"
#include "vnum.h"
#include "vsa.h"
#include "vss.h"
#include "vtcp.h"

/*--------------------------------------------------------------------*/

static void
vcc_cstrcat(struct vcc *tl, struct vsb *vsb)
{
	struct token *t1;

	assert(tl->t->tok == CSTR);
	VSB_cat(vsb, tl->t->dec);

	t1 = vcc_PeekToken(tl);
	AN(t1);

	while (t1->tok == '+') {
		vcc_NextToken(tl);
		SkipToken(tl, '+');
		ExpectErr(tl, CSTR);
		VSB_cat(vsb, tl->t->dec);
		t1 = vcc_PeekToken(tl);
		AN(t1);
	}
}

void
vcc_regexp(struct vcc *tl, struct vsb *vgc_name)
{
	struct vsb *pattern;
	struct token *t0;
	char buf[BUFSIZ];
	vre_t *t;
	int error, erroroffset;
	struct inifin *ifp;

	t0 = tl->t;
	pattern = VSB_new_auto();
	AN(pattern);
	vcc_cstrcat(tl, pattern);
	AZ(VSB_finish(pattern));

	t = VRE_compile(VSB_data(pattern), 0, &error, &erroroffset, 0);
	if (t == NULL) {
		VSB_cat(tl->sb, "Regexp compilation error:\n\n");
		AZ(VRE_error(tl->sb, error));
		VSB_cat(tl->sb, "\n\n");
		vcc_ErrWhere2(tl, t0, tl->t);
		VSB_destroy(&pattern);
		return;
	}
	VRE_free(&t);
	bprintf(buf, "VGC_re_%u", tl->unique++);
	if (vgc_name)
		VSB_cat(vgc_name, buf);

	Fh(tl, 0, "static struct vre *%s;\n", buf);
	ifp = New_IniFin(tl);
	VSB_printf(ifp->ini, "\tVPI_re_init(&%s, ",buf);
	VSB_quote(ifp->ini, VSB_data(pattern), -1, VSB_QUOTE_CSTR);
	VSB_cat(ifp->ini, ");");
	VSB_printf(ifp->fin, "\t\tVPI_re_fini(%s);", buf);
	VSB_destroy(&pattern);
}

/*
 * The IPv6 crew royally screwed up the entire idea behind
 * struct sockaddr, see libvarnish/vsa.c for blow-by-blow account.
 *
 * There is no sane or even remotely portable way to initialize
 * a sockaddr for random protocols at compile time.
 *
 * In our case it is slightly more tricky than that, because we don't
 * even want to #include the struct sockaddr* definitions.
 *
 * Instead we make sure the sockaddr is sane (for our values of
 * sane) and dump it as our own "struct suckaddr" type, in binary,
 * using the widest integer type, hoping that this will ensure sufficient
 * alignment.
 */

static void
vcc_suckaddr(struct vcc *tl, const char *host, const struct suckaddr *vsa,
    const char **ip, const char **ip_ascii, const char **p_ascii)
{
	char a[VTCP_ADDRBUFSIZE];
	char p[VTCP_PORTBUFSIZE];
	const int sz = sizeof(unsigned long long);
	const unsigned n = (vsa_suckaddr_len + sz - 1) / sz;
	unsigned long long b[n];
	unsigned len;
	char *q;

	VTCP_name(vsa, a, sizeof a, p, sizeof p);
	Fh(tl, 0, "\n/* \"%s\" -> %s */\n", host, a);
	if (ip_ascii != NULL)
		*ip_ascii = TlDup(tl, a);
	if (p_ascii != NULL && *p_ascii == NULL)
		*p_ascii = TlDup(tl, p);

	Fh(tl, 0, "static const unsigned long long");
	Fh(tl, 0, " suckaddr_%u[%d] = {\n", tl->unique, n);
	memcpy(b, vsa, vsa_suckaddr_len);
	for (len = 0; len < n; len++)
		Fh(tl, 0, "%s    0x%0*llxULL",
		    len ? ",\n" : "", sz * 2, b[len]);
	Fh(tl, 0, "\n};\n");

	q = TlAlloc(tl, 40);
	AN(q);
	assert(snprintf(q, 40, "(const void*)suckaddr_%u", tl->unique) < 40);
	*ip = q;
	tl->unique++;
}

/*--------------------------------------------------------------------
 * This routine is a monster, but at least we only have one such monster.
 * Look up a IP number, and return IPv4/IPv6 address as VGC produced names
 * and optionally ascii strings.
 *
 * For IP compile time constants we only want one IP#, but it can be
 * IPv4 or IPv6.
 *
 * For backends, we accept up to one IPv4 and one IPv6.
 */

struct rss {
	unsigned		magic;
#define RSS_MAGIC		0x11e966ab

	const struct suckaddr	*vsa4;
	const struct suckaddr	*vsa6;
	struct vsb		*vsb;
	int			retval;
	int			wrong;
};

static int v_matchproto_(vss_resolved_f)
rs_callback(void *priv, const struct suckaddr *vsa)
{
	struct rss *rss;
	int v;
	char a[VTCP_ADDRBUFSIZE];
	char p[VTCP_PORTBUFSIZE];

	CAST_OBJ_NOTNULL(rss, priv, RSS_MAGIC);
	assert(VSA_Sane(vsa));

	v = VSA_Get_Proto(vsa);
	assert(v != AF_UNIX);
	VTCP_name(vsa, a, sizeof a, p, sizeof p);
	VSB_printf(rss->vsb, "\t%s:%s\n", a, p);
	if (v == AF_INET) {
		if (rss->vsa4 == NULL)
			rss->vsa4 = VSA_Clone(vsa);
		else if (VSA_Compare(vsa, rss->vsa4))
			rss->wrong++;
		rss->retval++;
	} else if (v == AF_INET6) {
		if (rss->vsa6 == NULL)
			rss->vsa6 = VSA_Clone(vsa);
		else if (VSA_Compare(vsa, rss->vsa6))
			rss->wrong++;
		rss->retval++;
	}
	return (0);
}

void
Resolve_Sockaddr(struct vcc *tl,
    const char *host,
    const char *def_port,
    const char **ipv4,
    const char **ipv4_ascii,
    const char **ipv6,
    const char **ipv6_ascii,
    const char **p_ascii,
    int maxips,
    const struct token *t_err,
    const char *errid)
{
	int error;
	struct rss rss[1];
	const char *err;

	*ipv4 = NULL;
	*ipv6 = NULL;
	if (p_ascii != NULL)
		*p_ascii = NULL;

	INIT_OBJ(rss, RSS_MAGIC);
	rss->vsb = VSB_new_auto();
	AN(rss->vsb);

	error = VSS_resolver(host, def_port, rs_callback, rss, &err);
	AZ(VSB_finish(rss->vsb));
	if (err != NULL) {
		VSB_printf(tl->sb,
		    "%s '%.*s' could not be resolved to an IP address:\n"
		    "\t%s\n"
		    "(Sorry if that error message is gibberish.)\n",
		    errid, PF(t_err), err);
		vcc_ErrWhere(tl, t_err);
		if (rss->vsa4 != NULL)
			VSA_free(&rss->vsa4);
		if (rss->vsa6 != NULL)
			VSA_free(&rss->vsa6);
		VSB_destroy(&rss->vsb);
		ZERO_OBJ(rss, sizeof rss);
		return;
	}
	AZ(error);
	if (rss->vsa4 != NULL) {
		vcc_suckaddr(tl, host, rss->vsa4, ipv4, ipv4_ascii, p_ascii);
		VSA_free(&rss->vsa4);
	}
	if (rss->vsa6 != NULL) {
		vcc_suckaddr(tl, host, rss->vsa6, ipv6, ipv6_ascii, p_ascii);
		VSA_free(&rss->vsa6);
	}
	if (rss->retval == 0) {
		VSB_printf(tl->sb,
		    "%s '%.*s': resolves to "
		    "neither IPv4 nor IPv6 addresses.\n",
		    errid, PF(t_err) );
		vcc_ErrWhere(tl, t_err);
	}
	if (rss->wrong || rss->retval > maxips) {
		VSB_printf(tl->sb,
		    "%s %.*s: resolves to too many addresses.\n"
		    "Only one IPv4 %s IPv6 are allowed.\n"
		    "Please specify which exact address "
		    "you want to use, we found all of these:\n%s",
		    errid, PF(t_err),
		    maxips > 1 ? "and one" :  "or",
		    VSB_data(rss->vsb));
		vcc_ErrWhere(tl, t_err);
	}
	VSB_destroy(&rss->vsb);
	ZERO_OBJ(rss, sizeof rss);
}

/*--------------------------------------------------------------------
* Recognize boolean const "true" or "false"
*/

uint8_t
vcc_BoolVal(struct vcc *tl)
{
	struct symbol* sym;

	if (tl->t->tok != ID) {
		VSB_cat(tl->sb, "Expected \"true\" or \"false\"\n");
		vcc_ErrWhere(tl, tl->t);
		return (0);
	}
	sym = VCC_SymbolGet(tl, SYM_MAIN, SYM_NONE, SYMTAB_NOERR, XREF_NONE);
	if (sym == NULL || sym->type != BOOL) {
		VSB_cat(tl->sb, "Expected \"true\" or \"false\"\n");
		vcc_ErrWhere(tl, tl->t);
		return (0);
	}
	return (sym->eval_priv != NULL);
}

/*--------------------------------------------------------------------
 * Recognize and convert units of duration, return seconds.
 */

double
vcc_DurationUnit(struct vcc *tl)
{
	double sc;

	assert(tl->t->tok == ID);
	sc = VNUM_duration_unit(1.0, tl->t->b, tl->t->e);
	if (!isnan(sc)) {
		vcc_NextToken(tl);
		return (sc);
	}
	VSB_cat(tl->sb, "Unknown duration unit ");
	vcc_ErrToken(tl, tl->t);
	VSB_printf(tl->sb, "\n%s\n", VNUM_LEGAL_DURATION);
	vcc_ErrWhere(tl, tl->t);
	return (1.0);
}

/*--------------------------------------------------------------------
 * Recognize and convert { CNUM } to unsigned value
 * The tokenizer made sure we only get digits.
 */

uint64_t
vcc_UintVal(struct vcc *tl)
{
	int64_t retval;

	if (tl->t->tok != CNUM) {
		Expect(tl, CNUM);
		return (0);
	}
	retval = (int64_t)round(tl->t->num);
	if (retval < 0) {
		VSB_cat(tl->sb, "UINT cannot be negative\n");
		vcc_ErrWhere(tl, tl->t);
		return (0);
	}
	vcc_NextToken(tl);
	return (retval);
}

static double
vcc_DoubleVal(struct vcc *tl)
{
	double retval;

	if (tl->t->tok != CNUM && tl->t->tok != FNUM) {
		Expect(tl, CNUM);
		return (0);
	}
	retval = tl->t->num;
	vcc_NextToken(tl);
	return (retval);
}

/*--------------------------------------------------------------------*/

void
vcc_Duration(struct vcc *tl, double *d)
{
	double v, sc;

	v = vcc_DoubleVal(tl);
	ERRCHK(tl);
	ExpectErr(tl, ID);
	sc = vcc_DurationUnit(tl);
	*d = v * sc;
}

/*--------------------------------------------------------------------*/

void
vcc_ByteVal(struct vcc *tl, VCL_INT *d)
{
	double v;
	VCL_INT retval;
	const char *errtxt;

	if (tl->t->tok != CNUM && tl->t->tok != FNUM) {
		Expect(tl, CNUM);
		return;
	}
	v = tl->t->num;
	vcc_NextToken(tl);
	if (tl->t->tok != ID) {
		VSB_cat(tl->sb, "Expected BYTES unit (B, KB, MB...) got ");
		vcc_ErrToken(tl, tl->t);
		VSB_cat(tl->sb, "\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	retval = VNUM_bytes_unit(v, tl->t->b, tl->t->e, 0, &errtxt);
	if (errno) {
		VSB_cat(tl->sb, errtxt);
		vcc_ErrToken(tl, tl->t);
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	vcc_NextToken(tl);
	*d = retval;
}

/*--------------------------------------------------------------------*/

int
vcc_IsFlagRaw(struct vcc *tl, const struct token *t1, const struct token *t2)
{

	if (t1->tok != '-' && t1->tok != '+')
		return (-1);
	if (t2->b != t1->e) {
		VSB_cat(tl->sb, "Expected a flag at:\n");
		vcc_ErrWhere(tl, t1);
		return (-1);
	}
	return (t1->tok == '+' ? 1 : 0);
}

int
vcc_IsFlag(struct vcc *tl)
{
	struct token *t;
	int retval;

	t = vcc_PeekToken(tl);
	if (t == NULL)
		return (-1);
	retval = vcc_IsFlagRaw(tl, tl->t, t);
	if (retval >= 0)
		vcc_NextToken(tl);
	return (retval);
}

char *
vcc_Dup_be(const char *b, const char *e)
{
	char *p;

	AN(b);
	if (e == NULL)
		e = strchr(b, '\0');
	AN(e);
	assert(e >= b);

	p = strndup(b, e - b);
	AN(p);
	return (p);
}

int
vcc_Has_vcl_prefix(const char *b)
{
	return (
	    (b[0] == 'v' || b[0] == 'V') &&
	    (b[1] == 'c' || b[1] == 'C') &&
	    (b[2] == 'l' || b[2] == 'L') &&
	    (b[3] == '_')
	);
}
