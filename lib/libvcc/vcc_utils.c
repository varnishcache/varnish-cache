/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "vcc_compile.h"

#include "vre.h"
#include "vsa.h"
#include "vss.h"
#include "vtcp.h"

/*--------------------------------------------------------------------*/

void
vcc_regexp(struct vcc *tl, struct vsb *vgc_name)
{
	char buf[BUFSIZ];
	vre_t *t;
	const char *error;
	int erroroffset;
	struct inifin *ifp;

	Expect(tl, CSTR);
	if (tl->err)
		return;
	t = VRE_compile(tl->t->dec, 0, &error, &erroroffset);
	if (t == NULL) {
		VSB_printf(tl->sb,
		    "Regexp compilation error:\n\n%s\n\n", error);
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	VRE_free(&t);
	bprintf(buf, "VGC_re_%u", tl->unique++);
	if (vgc_name)
		VSB_cat(vgc_name, buf);

	Fh(tl, 0, "static void *%s;\n", buf);
	ifp = New_IniFin(tl);
	VSB_printf(ifp->ini, "\tVRT_re_init(&%s, ",buf);
	EncToken(ifp->ini, tl->t);
	VSB_printf(ifp->ini, ");");
	VSB_printf(ifp->fin, "\t\tVRT_re_fini(%s);", buf);
	vcc_NextToken(tl);
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
 * using the widest integertype, hoping that this will ensure sufficient
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
	int len;
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

	struct suckaddr		*vsa4;
	struct suckaddr		*vsa6;
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
	struct rss *rss;
	const char *err;

	*ipv4 = NULL;
	*ipv6 = NULL;
	if (p_ascii != NULL)
		*p_ascii = NULL;

	ALLOC_OBJ(rss, RSS_MAGIC);
	AN(rss);
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
		free(rss->vsa4);
		free(rss->vsa6);
		VSB_destroy(&rss->vsb);
		FREE_OBJ(rss);
		return;
	}
	AZ(error);
	if (rss->vsa4 != NULL) {
		vcc_suckaddr(tl, host, rss->vsa4, ipv4, ipv4_ascii, p_ascii);
		free(rss->vsa4);
	}
	if (rss->vsa6 != NULL) {
		vcc_suckaddr(tl, host, rss->vsa6, ipv6, ipv6_ascii, p_ascii);
		free(rss->vsa6);
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
	FREE_OBJ(rss);
}

/*
 * For UDS, we do not create a VSA. Check if it's an absolute path, can
 * be accessed, and is a socket. If so, just emit the path field and set
 * the IP suckaddrs to NULL.
 */
void
Emit_UDS_Path(struct vcc *tl, const struct token *t_path, const char *errid)
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
		VSB_printf(tl->sb, "%s: Cannot stat: %s\n", errid,
			   strerror(errno));
		vcc_ErrWhere(tl, t_path);
		return;
	}
	if (!S_ISSOCK(st.st_mode)) {
		VSB_printf(tl->sb, "%s: Not a socket:\n", errid);
		vcc_ErrWhere(tl, t_path);
		return;
	}
	Fb(tl, 0, "\t.path = \"%s\",\n", t_path->dec);
	Fb(tl, 0, "\t.ipv4_suckaddr = (void *) 0,\n");
	Fb(tl, 0, "\t.ipv6_suckaddr = (void *) 0,\n");
}

/*--------------------------------------------------------------------
 * Recognize and convert units of time, return seconds.
 */

double
vcc_TimeUnit(struct vcc *tl)
{
	double sc = 1.0;

	assert(tl->t->tok == ID);
	if (vcc_IdIs(tl->t, "ms"))
		sc = 1e-3;
	else if (vcc_IdIs(tl->t, "s"))
		sc = 1.0;
	else if (vcc_IdIs(tl->t, "m"))
		sc = 60.0;
	else if (vcc_IdIs(tl->t, "h"))
		sc = 60.0 * 60.0;
	else if (vcc_IdIs(tl->t, "d"))
		sc = 60.0 * 60.0 * 24.0;
	else if (vcc_IdIs(tl->t, "w"))
		sc = 60.0 * 60.0 * 24.0 * 7.0;
	else if (vcc_IdIs(tl->t, "y"))
		sc = 60.0 * 60.0 * 24.0 * 365.0;
	else {
		VSB_printf(tl->sb, "Unknown time unit ");
		vcc_ErrToken(tl, tl->t);
		VSB_printf(tl->sb,
		    ".  Legal are 'ms', 's', 'm', 'h', 'd', 'w' and 'y'\n");
		vcc_ErrWhere(tl, tl->t);
		return (1.0);
	}
	vcc_NextToken(tl);
	return (sc);
}

/*--------------------------------------------------------------------
 * Recognize and convert { CNUM } to unsigned value
 * The tokenizer made sure we only get digits.
 */

unsigned
vcc_UintVal(struct vcc *tl)
{
	unsigned d = 0;
	const char *p;

	Expect(tl, CNUM);
	for (p = tl->t->b; p < tl->t->e; p++) {
		d *= 10;
		d += *p - '0';
	}
	vcc_NextToken(tl);
	return (d);
}

/*--------------------------------------------------------------------
 * Recognize and convert { CNUM [ '.' [ CNUM ] ] } to double value
 * The tokenizer made sure we only get digits and a '.'
 */

void
vcc_NumVal(struct vcc *tl, double *d, int *frac)
{
	double e = 0.1;
	const char *p;

	*frac = 0;
	*d = 0.0;
	Expect(tl, CNUM);
	if (tl->err) {
		*d = NAN;
		return;
	}
	for (p = tl->t->b; p < tl->t->e; p++) {
		*d *= 10;
		*d += *p - '0';
	}
	vcc_NextToken(tl);
	if (tl->t->tok != '.')
		return;
	*frac = 1;
	vcc_NextToken(tl);
	if (tl->t->tok != CNUM)
		return;
	for (p = tl->t->b; p < tl->t->e; p++) {
		*d += (*p - '0') * e;
		e *= 0.1;
	}
	vcc_NextToken(tl);
}

static double
vcc_DoubleVal(struct vcc *tl)
{
	double d;
	int i;

	vcc_NumVal(tl, &d, &i);
	return (d);
}

/*--------------------------------------------------------------------*/

void
vcc_Duration(struct vcc *tl, double *d)
{
	double v, sc;

	v = vcc_DoubleVal(tl);
	ERRCHK(tl);
	ExpectErr(tl, ID);
	sc = vcc_TimeUnit(tl);
	*d = v * sc;
}

/*--------------------------------------------------------------------*/

void
vcc_ByteVal(struct vcc *tl, double *d)
{
	double v, sc;

	v = vcc_DoubleVal(tl);
	ERRCHK(tl);
	if (tl->t->tok != ID) {
		VSB_printf(tl->sb, "Expected BYTES unit (B, KB, MB...) got ");
		vcc_ErrToken(tl, tl->t);
		VSB_printf(tl->sb, "\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	if (vcc_IdIs(tl->t, "B"))
		sc = 1.;
	else if (vcc_IdIs(tl->t, "KB"))
		sc = 1024.;
	else if (vcc_IdIs(tl->t, "MB"))
		sc = 1024. * 1024.;
	else if (vcc_IdIs(tl->t, "GB"))
		sc = 1024. * 1024. * 1024.;
	else if (vcc_IdIs(tl->t, "TB"))
		sc = 1024. * 1024. * 1024. * 1024.;
	else {
		VSB_printf(tl->sb, "Unknown BYTES unit ");
		vcc_ErrToken(tl, tl->t);
		VSB_printf(tl->sb,
		    ".  Legal are 'B', 'KB', 'MB', 'GB' and 'TB'\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	vcc_NextToken(tl);
	*d = v * sc;
}

