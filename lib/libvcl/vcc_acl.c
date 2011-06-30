/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Varnish Software AS
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

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#include "vsb.h"
#include "vrt.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

struct acl_e {
	VTAILQ_ENTRY(acl_e)	list;
	unsigned char		data[VRT_ACL_MAXADDR + 1];
	unsigned		mask;
	unsigned		not;
	unsigned		para;
	struct token		*t_addr;
	struct token		*t_mask;
};

/* Compare two acl rules for ordering */

#define CMP(a, b)							\
	do {								\
		if ((a) < (b))						\
			return (-1);					\
		else if ((b) < (a))					\
			return (1);					\
	} while (0)

static int
vcl_acl_cmp(struct acl_e *ae1, struct acl_e *ae2)
{
	unsigned char *p1, *p2;
	unsigned m;

	p1 = ae1->data;
	p2 = ae2->data;
	m = ae1->mask;
	if (ae2->mask < m)
		m = ae2->mask;
	for (; m >= 8; m -= 8) {
		CMP(*p1, *p2);
		p1++;
		p2++;
	}
	if (m) {
		m = 0xff00 >> m;
		m &= 0xff;
		CMP(*p1 & m, *p2 & m);
	}
	/* Long mask is less than short mask */
	CMP(ae2->mask, ae1->mask);

	return (0);
}


static void
vcc_acl_add_entry(struct vcc *tl, const struct acl_e *ae, int l,
    const unsigned char *u, int fam)
{
	struct acl_e *ae2, *aen;
	int i;

	if (fam == PF_INET && ae->mask > 32) {
		VSB_printf(tl->sb,
		    "Too wide mask (%u) for IPv4 address", ae->mask);
		vcc_ErrWhere(tl, ae->t_mask);
		return;
	}
	if (fam == PF_INET6 && ae->mask > 128) {
		VSB_printf(tl->sb,
		    "Too wide mask (%u) for IPv6 address", ae->mask);
		vcc_ErrWhere(tl, ae->t_mask);
		return;
	}

	/* Make a copy from the template */
	aen = TlAlloc(tl, sizeof *ae2);
	AN(aen);
	*aen = *ae;

	/* We treat family as part of address, it saves code */
	assert(fam <= 0xff);
	aen->data[0] = fam & 0xff;
	aen->mask += 8;

	memcpy(aen->data + 1, u, l);

	VTAILQ_FOREACH(ae2, &tl->acl, list) {
		i = vcl_acl_cmp(aen, ae2);
		if (i == 0) {
			/*
			 * If the two rules agree, silently ignore it
			 * XXX: is that counter intuitive ?
			 */
			if (aen->not == ae2->not)
				return;
			VSB_printf(tl->sb, "Conflicting ACL entries:\n");
			vcc_ErrWhere(tl, ae2->t_addr);
			VSB_printf(tl->sb, "vs:\n");
			vcc_ErrWhere(tl, aen->t_addr);
			return;
		}
		/*
		 * We could eliminate pointless rules here, for instance in:
		 *	"10.1.0.1";
		 *	"10.1";
		 * The first rule is clearly pointless, as the second one
		 * covers it.
		 *
		 * We do not do this however, because the shmlog may
		 * be used to gather statistics.
		 */
		if (i < 0) {
			VTAILQ_INSERT_BEFORE(ae2, aen, list);
			return;
		}
	}
	VTAILQ_INSERT_TAIL(&tl->acl, aen, list);
}

static void
vcc_acl_try_getaddrinfo(struct vcc *tl, struct acl_e *ae)
{
	struct addrinfo *res0, *res, hint;
	struct sockaddr_in *sin4;
	struct sockaddr_in6 *sin6;
	unsigned char *u, i4, i6;
	int error;

	memset(&hint, 0, sizeof hint);
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(ae->t_addr->dec, "0", &hint, &res0);
	if (error) {
		if (ae->para) {
			VSB_printf(tl->sb,
			    "Warning: %s ignored\n  -- %s\n",
			    ae->t_addr->dec, gai_strerror(error));
			Fh(tl, 1, "/* Ignored ACL entry: %s%s",
			    ae->para ? "\"(\" " : "", ae->not ? "\"!\" " : "");
			EncToken(tl->fh, ae->t_addr);
			if (ae->t_mask)
				Fh(tl, 0, "/%u", ae->mask);
			Fh(tl, 0, "%s\n", ae->para ? " \")\"" : "");
			Fh(tl, 1, " * getaddrinfo:  %s */\n",
			     gai_strerror(error));
		} else {
			VSB_printf(tl->sb,
			    "DNS lookup(%s): %s\n",
			    ae->t_addr->dec, gai_strerror(error));
			vcc_ErrWhere(tl, ae->t_addr);
		}
		return;
	}

	i4 = i6 = 0;
	for(res = res0; res != NULL; res = res->ai_next) {
		switch(res->ai_family) {
		case PF_INET:
			assert(PF_INET < 256);
			sin4 = (void*)res->ai_addr;
			assert(sizeof(sin4->sin_addr) == 4);
			u = (void*)&sin4->sin_addr;
			if (ae->t_mask == NULL)
				ae->mask = 32;
			i4++;
			vcc_acl_add_entry(tl, ae, 4, u, res->ai_family);
			break;
		case PF_INET6:
			assert(PF_INET6 < 256);
			sin6 = (void*)res->ai_addr;
			assert(sizeof(sin6->sin6_addr) == 16);
			u = (void*)&sin6->sin6_addr;
			if (ae->t_mask == NULL)
				ae->mask = 128;
			i6++;
			vcc_acl_add_entry(tl, ae, 16, u, res->ai_family);
			break;
		default:
			VSB_printf(tl->sb,
			    "Ignoring unknown protocol family (%d) for %.*s\n",
				res->ai_family, PF(ae->t_addr));
			continue;
		}
		ERRCHK(tl);
	}
	freeaddrinfo(res0);

	if (ae->t_mask != NULL && i4 > 0 && i6 > 0) {
		VSB_printf(tl->sb,
		    "Mask (%u) specified, but string resolves to"
		    " both IPv4 and IPv6 addresses.\n", ae->mask);
		vcc_ErrWhere(tl, ae->t_mask);
		return;
	}
}

/*--------------------------------------------------------------------
 * Ancient stupidity on the part of X/Open and other standards orgs
 * dictate that "192.168" be translated to 192.0.0.168.  Ever since
 * CIDR happened, "192.168/16" notation has been used, but appearantly
 * no API supports parsing this, so roll our own.
 */

static int
vcc_acl_try_netnotation(struct vcc *tl, struct acl_e *ae)
{
	unsigned char b[4];
	int i, j, k;
	unsigned u;
	const char *p;

	memset(b, 0, sizeof b);
	p = ae->t_addr->dec;
	for (i = 0; i < 4; i++) {
		j = sscanf(p, "%u%n", &u, &k);
		if (j != 1)
			return (0);
		if (u & ~0xff)
			return (0);
		b[i] = (unsigned char)u;
		if (p[k] == '\0')
			break;
		if (p[k] != '.')
			return (0);
		p += k + 1;
	}
	if (ae->t_mask == NULL)
		ae->mask = 8 + 8 * i;
	vcc_acl_add_entry(tl, ae, 4, b, AF_INET);
	return (1);
}

static void
vcc_acl_entry(struct vcc *tl)
{
	struct acl_e *ae;

	ae = TlAlloc(tl, sizeof *ae);
	AN(ae);

	if (tl->t->tok == '!') {
		ae->not = 1;
		vcc_NextToken(tl);
	}

	if (tl->t->tok == '(') {
		ae->para = 1;
		vcc_NextToken(tl);
	}

	if (!ae->not && tl->t->tok == '!') {
		ae->not = 1;
		vcc_NextToken(tl);
	}

	ExpectErr(tl, CSTR);
	ae->t_addr = tl->t;
	vcc_NextToken(tl);

	if (tl->t->tok == '/') {
		vcc_NextToken(tl);
		ae->t_mask = tl->t;
		ExpectErr(tl, CNUM);
		ae->mask = vcc_UintVal(tl);
	}

	if (ae->para)
		SkipToken(tl, ')');

	if (!vcc_acl_try_netnotation(tl, ae)) {
		ERRCHK(tl);
		vcc_acl_try_getaddrinfo(tl, ae);
	}
	ERRCHK(tl);
}

/*********************************************************************
 * Emit a function to match the ACL we have collected
 */

/*
 * XXX: this is semi-silly.  We try really hard to not depend in the
 * XXX: systems include files while compiling VCL, but we need to know
 * XXX: the size of the sa_familiy member.
 * XXX: FlexeLint complains about these antics, so isolate it in a
 * XXX: separate function.
 */

/*lint -save -e506 -e774 -e550 */
static void
c_is_a_silly_language(const struct vcc *tl)
{
	struct sockaddr sa;

	assert(sizeof (unsigned char) == 1);
	assert(sizeof (unsigned short) == 2);
	assert(sizeof (unsigned int) == 4);
	if (sizeof sa.sa_family == 1)
		Fh(tl, 0, "\tunsigned char fam;\n");
	else if (sizeof sa.sa_family == 2)
		Fh(tl, 0, "\tunsigned short fam;\n");
	else if (sizeof sa.sa_family == 4)
		Fh(tl, 0, "\tunsigned int fam;\n");
	else
		assert(0 == __LINE__);
}
/*lint -restore */

static void
vcc_acl_emit(const struct vcc *tl, const char *acln, int anon)
{
	struct acl_e *ae;
	int depth, l, m, i;
	unsigned at[VRT_ACL_MAXADDR + 1];
	const char *oc;

	Fh(tl, 0, "\nstatic int\n");
	Fh(tl, 0, "match_acl_%s_%s(const struct sess *sp, const void *p)\n",
	    anon ? "anon" : "named", acln);
	Fh(tl, 0, "{\n");
	Fh(tl, 0, "\tconst unsigned char *a;\n");
	c_is_a_silly_language(tl);

	Fh(tl, 0, "\n");
	Fh(tl, 0, "\ta = p;\n");
	Fh(tl, 0, "\tVRT_memmove(&fam, a + %d, sizeof fam);\n",
	    offsetof(struct sockaddr, sa_family));
	Fh(tl, 0, "\tif (fam == %d)\n", PF_INET);
	Fh(tl, 0, "\t\ta += %d;\n", offsetof(struct sockaddr_in, sin_addr));
	Fh(tl, 0, "\telse if (fam == %d)\n", PF_INET6);
	Fh(tl, 0, "\t\ta += %d;\n", offsetof(struct sockaddr_in6, sin6_addr));
	Fh(tl, 0, "\telse {\n");
	Fh(tl, 0, "\t\tVRT_acl_log(sp, \"NO_FAM %s\");\n", acln);
	Fh(tl, 0, "\t\treturn(0);\n");
	Fh(tl, 0, "\t}\n\n");
	depth = -1;
	oc = 0;
	at[0] = 256;
	VTAILQ_FOREACH(ae, &tl->acl, list) {

		/* Find how much common prefix we have */
		for (l = 0; l <= depth && l * 8 < ae->mask; l++) {
			assert(l >= 0);
			if (ae->data[l] != at[l])
				break;
		}

		/* Back down, if necessary */
		oc = "";
		while (l <= depth) {
			Fh(tl, 0, "\t%*s}\n", -depth, "");
			depth--;
			oc = "else ";
		}

		m = ae->mask;
		m -= l * 8;

		/* Do whole byte compares */
		for (i = l; m >= 8; m -= 8, i++) {
			if (i == 0)
				Fh(tl, 0, "\t%*s%sif (fam == %d) {\n",
				    -i, "", oc, ae->data[i]);
			else
				Fh(tl, 0, "\t%*s%sif (a[%d] == %d) {\n",
				    -i, "", oc, i - 1, ae->data[i]);
			at[i] = ae->data[i];
			depth = i;
			oc = "";
		}

		if (m > 0) {
			/* Do fractional byte compares */
			Fh(tl, 0, "\t%*s%sif ((a[%d] & 0x%x) == %d) {\n",
			    -i, "", oc, i - 1, (0xff00 >> m) & 0xff,
			    ae->data[i] & ((0xff00 >> m) & 0xff));
			at[i] = 256;
			depth = i;
			oc = "";
		}

		i = (ae->mask + 7) / 8;

		if (!anon) {
			Fh(tl, 0, "\t%*sVRT_acl_log(sp, \"%sMATCH %s \" ",
			    -i, "", ae->not ? "NEG_" : "", acln,
			    PF(ae->t_addr));
			EncToken(tl->fh, ae->t_addr);
			if (ae->t_mask != NULL)
				Fh(tl, 0, " \"/%.*s\" ", PF(ae->t_mask));
			Fh(tl, 0, ");\n");
		}

		Fh(tl, 0, "\t%*sreturn (%d);\n", -i, "", ae->not ? 0 : 1);
	}

	/* Unwind */
	for (; 0 <= depth; depth--)
		Fh(tl, 0, "\t%*.*s}\n", depth, depth, "");

	/* Deny by default */
	if (!anon)
		Fh(tl, 0, "\tVRT_acl_log(sp, \"NO_MATCH %s\");\n", acln);
	Fh(tl, 0, "\treturn (0);\n}\n");
}

void
vcc_Acl_Hack(struct vcc *tl, char *b)
{
	char acln[32];
	unsigned tcond;

	VTAILQ_INIT(&tl->acl);
	tcond = tl->t->tok;
	vcc_NextToken(tl);
	bprintf(acln, "%u", tl->unique++);
	vcc_acl_entry(tl);
	vcc_acl_emit(tl, acln, 1);
	sprintf(b, "%smatch_acl_anon_%s(sp, \v1)",
	    (tcond == T_NEQ ? "!" : ""), acln);
}

void
vcc_Acl(struct vcc *tl)
{
	struct token *an;
	int i;
	char acln[1024];

	vcc_NextToken(tl);
	VTAILQ_INIT(&tl->acl);

	ExpectErr(tl, ID);
	an = tl->t;
	vcc_NextToken(tl);

	i = vcc_AddDef(tl, an, SYM_ACL);
	if (i > 1) {
		VSB_printf(tl->sb, "ACL %.*s redefined\n", PF(an));
		vcc_ErrWhere(tl, an);
		return;
	}
	bprintf(acln, "%.*s", PF(an));

	SkipToken(tl, '{');

	while (tl->t->tok != '}') {
		vcc_acl_entry(tl);
		ERRCHK(tl);
		SkipToken(tl, ';');
	}
	SkipToken(tl, '}');

	vcc_acl_emit(tl, acln, 0);
}
