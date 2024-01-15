/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Varnish Software AS
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

#include <sys/socket.h>

#include <netinet/in.h>

#include <netdb.h>
#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"
#include <vtcp.h>
#include <vtree.h>
#include <vsa.h>

#define ACL_MAXADDR	(sizeof(struct in6_addr) + 1)

VRBT_HEAD(acl_tree, acl_e);

struct acl {
	unsigned		magic;
#define VCC_ACL_MAGIC		0xb9fb3cd0

	int			flag_log;
	int			flag_fold;
	int			flag_pedantic;
	int			flag_table;

	struct acl_tree		acl_tree;
};

struct acl_e {
	unsigned		magic;
#define VCC_ACL_E_MAGIC	0xcac81e23
	VRBT_ENTRY(acl_e)	branch;
	unsigned char		data[ACL_MAXADDR];
	unsigned		mask;
	unsigned		not;
	unsigned		para;
	unsigned		overlapped;
	char			*addr;
	const char		*fixed;
	struct token		*t_addr;
	struct token		*t_mask;
};

enum acl_cmp_e {
	ACL_EQ = 0,
	ACL_LT = -1,		// a < b
	ACL_GT = 1,		// b > a
	ACL_CONTAINED = -2,	// b contains a
	ACL_CONTAINS = 2,	// a contains b
	ACL_LEFT = -3,		// a + 1 == b
	ACL_RIGHT = 3		// a == b + 1
};

static void vcc_acl_insert_entry(struct vcc *, struct acl_e **);

/*
 * Compare two acl rules for relation
 */

#define CMP(n, a, b)							\
	do {								\
		if ((a) < (b))						\
			return (enum acl_cmp_e)(-n);			\
		else if ((b) < (a))					\
			return (n);					\
	} while (0)

#define CMPA(a, b)							\
	do {								\
		if (((a) | 1) == (b))					\
			return (ACL_LEFT);				\
		else if (((b) | 1) == (a))				\
			return (ACL_RIGHT);				\
	} while (0)

static void
vcl_acl_free(struct acl_e **aep)
{
	struct acl_e *a;

	TAKE_OBJ_NOTNULL(a, aep, VCC_ACL_E_MAGIC);
	free(a->addr);
	FREE_OBJ(a);
}

static enum acl_cmp_e
vcl_acl_cmp(const struct acl_e *ae1, const struct acl_e *ae2)
{
	const unsigned char *p1, *p2;
	unsigned m;
	unsigned char a1, a2;

	CHECK_OBJ_NOTNULL(ae1, VCC_ACL_E_MAGIC);
	CHECK_OBJ_NOTNULL(ae2, VCC_ACL_E_MAGIC);

	p1 = ae1->data;
	p2 = ae2->data;
	m = vmin_t(unsigned, ae1->mask, ae2->mask);
	for (; m >= 8; m -= 8) {
		if (m == 8 && ae1->mask == ae2->mask)
			CMPA(*p1, *p2);
		CMP(ACL_GT, *p1, *p2);
		p1++;
		p2++;
	}
	if (m) {
		assert (m < 8);
		a1 = *p1 >> (8 - m);
		a2 = *p2 >> (8 - m);
		if (ae1->mask == ae2->mask)
			CMPA(a1, a2);
		CMP(ACL_GT, a1, a2);
	} else if (ae1->mask == ae2->mask) {
		CMPA(*p1, *p2);
	}
	/* Long mask is less than short mask */
	CMP(ACL_CONTAINS, ae2->mask, ae1->mask);

	return (ACL_EQ);
}

static int
vcl_acl_disjoint(const struct acl_e *ae1, const struct acl_e *ae2)
{
	const unsigned char *p1, *p2;
	unsigned m;

	CHECK_OBJ_NOTNULL(ae1, VCC_ACL_E_MAGIC);
	CHECK_OBJ_NOTNULL(ae2, VCC_ACL_E_MAGIC);

	p1 = ae1->data;
	p2 = ae2->data;
	m = vmin_t(unsigned, ae1->mask, ae2->mask);
	for (; m >= 8; m -= 8) {
		CMP(ACL_GT, *p1, *p2);
		p1++;
		p2++;
	}
	if (m) {
		m = 0xff00 >> m;
		m &= 0xff;
		CMP(ACL_GT, *p1 & m, *p2 & m);
	}
	return (0);
}

VRBT_GENERATE_INSERT_COLOR(acl_tree, acl_e, branch, static)
VRBT_GENERATE_INSERT_FINISH(acl_tree, acl_e, branch, static)
VRBT_GENERATE_INSERT(acl_tree, acl_e, branch, vcl_acl_cmp, static)
VRBT_GENERATE_REMOVE_COLOR(acl_tree, acl_e, branch, static)
VRBT_GENERATE_REMOVE(acl_tree, acl_e, branch, static)
VRBT_GENERATE_MINMAX(acl_tree, acl_e, branch, static)
VRBT_GENERATE_NEXT(acl_tree, acl_e, branch, static)
VRBT_GENERATE_PREV(acl_tree, acl_e, branch, static)

static char *
vcc_acl_chk(struct vcc *tl, const struct acl_e *ae, const int l,
    unsigned char *p, int fam)
{
	const unsigned char *u;
	char h[VTCP_ADDRBUFSIZE];
	char t[VTCP_ADDRBUFSIZE + 10];
	v_vla_(char, s, vsa_suckaddr_len);
	char *r = NULL;
	const struct suckaddr *sa;
	unsigned m;
	int ll, ret = 0;

	u = p;
	ll = l;
	m = ae->mask;

	p += m / 8;
	ll -= m / 8;
	assert (ll >= 0);
	m %= 8;

	if (m && ((unsigned)*p << m & 0xff) != 0) {
		ret = 1;
		m = 0xff00 >> m;
		*p &= m;
	}
	if (m) {
		p++;
		ll--;
	}

	for ( ; ll > 0; p++, ll--) {
		if (*p == 0)
			continue;
		ret = 1;
		*p = 0;
	}
	if (ret == 0)
		return (NULL);

	sa = VSA_BuildFAP(s, fam, u, l, NULL, 0);
	AN(sa);
	VTCP_name(sa, h, sizeof h, NULL, 0);
	bprintf(t, "%s/%d", h, ae->mask);
	if (tl->acl->flag_pedantic != 0) {
		VSB_cat(tl->sb, "Non-zero bits in masked part, ");
		VSB_printf(tl->sb, "(maybe use %s ?)\n", t);
		vcc_ErrWhere(tl, ae->t_addr);
	}
	REPLACE(r, t);
	return (r);
}

static void
vcl_acl_fold(struct vcc *tl, struct acl_e **l, struct acl_e **r)
{
	enum acl_cmp_e cmp;

	AN(l);
	AN(r);
	CHECK_OBJ_NOTNULL(*l, VCC_ACL_E_MAGIC);
	CHECK_OBJ_NOTNULL(*r, VCC_ACL_E_MAGIC);

	if ((*l)->not || (*r)->not)
		return;

	cmp = vcl_acl_cmp(*l, *r);

	assert(cmp < 0);
	if (cmp == ACL_LT)
		return;

	do {
		switch (cmp) {
		case ACL_CONTAINED:
			VSB_cat(tl->sb, "ACL entry:\n");
			vcc_ErrWhere(tl, (*r)->t_addr);
			VSB_cat(tl->sb, "supersedes / removes:\n");
			vcc_ErrWhere(tl, (*l)->t_addr);
			vcc_Warn(tl);
			VRBT_REMOVE(acl_tree, &tl->acl->acl_tree, *l);
			FREE_OBJ(*l);
			*l = VRBT_PREV(acl_tree, &tl->acl->acl_tree, *r);
			break;
		case ACL_LEFT:
			(*l)->mask--;
			(*l)->fixed = "folded";
			VSB_cat(tl->sb, "ACL entry:\n");
			vcc_ErrWhere(tl, (*l)->t_addr);
			VSB_cat(tl->sb, "left of:\n");
			vcc_ErrWhere(tl, (*r)->t_addr);
			VSB_printf(tl->sb, "removing the latter and expanding "
			    "mask of the former by one to /%u\n",
			    (*l)->mask - 8);
			vcc_Warn(tl);
			VRBT_REMOVE(acl_tree, &tl->acl->acl_tree, *r);
			FREE_OBJ(*r);
			VRBT_REMOVE(acl_tree, &tl->acl->acl_tree, *l);
			vcc_acl_insert_entry(tl, l);
			return;
		default:
			INCOMPL();
		}
		if (*l == NULL || *r == NULL)
			break;
		cmp = vcl_acl_cmp(*l, *r);
	} while (cmp != ACL_LT);
}

static void
vcc_acl_insert_entry(struct vcc *tl, struct acl_e **aenp)
{
	struct acl_e *ae2, *l, *r;

	CHECK_OBJ_NOTNULL(*aenp, VCC_ACL_E_MAGIC);
	ae2 = VRBT_INSERT(acl_tree, &tl->acl->acl_tree, *aenp);
	if (ae2 != NULL) {
		if (ae2->not != (*aenp)->not) {
			VSB_cat(tl->sb, "Conflicting ACL entries:\n");
			vcc_ErrWhere(tl, ae2->t_addr);
			VSB_cat(tl->sb, "vs:\n");
			vcc_ErrWhere(tl, (*aenp)->t_addr);
		}
		return;
	}

	r = *aenp;
	*aenp = NULL;

	if (tl->acl->flag_fold == 0)
		return;

	l = VRBT_PREV(acl_tree, &tl->acl->acl_tree, r);
	if (l != NULL) {
		vcl_acl_fold(tl, &l, &r);
	}
	if (r == NULL)
		return;
	l = r;
	r = VRBT_NEXT(acl_tree, &tl->acl->acl_tree, l);
	if (r == NULL)
		return;
	vcl_acl_fold(tl, &l, &r);
}

static void
vcc_acl_add_entry(struct vcc *tl, const struct acl_e *ae, int l,
    unsigned char *u, int fam)
{
	struct acl_e *aen;

	if (fam == PF_INET && ae->mask > 32) {
		VSB_printf(tl->sb,
		    "Too wide mask (/%u) for IPv4 address\n", ae->mask);
		if (ae->t_mask != NULL)
			vcc_ErrWhere(tl, ae->t_mask);
		else
			vcc_ErrWhere(tl, ae->t_addr);
		return;
	}
	if (fam == PF_INET6 && ae->mask > 128) {
		VSB_printf(tl->sb,
		    "Too wide mask (/%u) for IPv6 address\n", ae->mask);
		vcc_ErrWhere(tl, ae->t_mask);
		return;
	}

	/* Make a copy from the template */
	ALLOC_OBJ(aen, VCC_ACL_E_MAGIC);
	AN(aen);
	*aen = *ae;
	aen->addr = strdup(ae->addr);
	AN(aen->addr);

	aen->fixed = vcc_acl_chk(tl, ae, l, u, fam);

	/* We treat family as part of address, it saves code */
	assert(fam <= 0xff);
	aen->data[0] = fam & 0xff;
	aen->mask += 8;

	assert(l + 1UL <= sizeof aen->data);
	memcpy(aen->data + 1L, u, l);

	vcc_acl_insert_entry(tl, &aen);
	if (aen != NULL)
		vcl_acl_free(&aen);
}

static void
vcc_acl_try_getaddrinfo(struct vcc *tl, struct acl_e *ae)
{
	struct addrinfo *res0, *res, hint;
	struct sockaddr_in *sin4;
	struct sockaddr_in6 *sin6;
	unsigned char *u, i4, i6;
	int error;

	CHECK_OBJ_NOTNULL(ae, VCC_ACL_E_MAGIC);
	memset(&hint, 0, sizeof hint);
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(ae->addr, "0", &hint, &res0);
	if (error) {
		if (ae->para) {
			VSB_printf(tl->sb,
			    "Warning: %s ignored\n  -- %s\n",
			    ae->addr, gai_strerror(error));
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
			    ae->addr, gai_strerror(error));
			vcc_ErrWhere(tl, ae->t_addr);
		}
		return;
	}

	i4 = i6 = 0;
	for (res = res0; res != NULL; res = res->ai_next) {
		switch (res->ai_family) {
		case PF_INET:
			i4++;
			break;
		case PF_INET6:
			i6++;
			break;
		default:
			VSB_printf(tl->sb,
			    "Ignoring unknown protocol family (%d) for %.*s\n",
				res->ai_family, PF(ae->t_addr));
			continue;
		}
	}

	if (ae->t_mask != NULL && i4 > 0 && i6 > 0) {
		VSB_printf(tl->sb,
		    "Mask (/%u) specified, but string resolves to"
		    " both IPv4 and IPv6 addresses.\n", ae->mask);
		vcc_ErrWhere(tl, ae->t_mask);
		freeaddrinfo(res0);
		return;
	}

	for (res = res0; res != NULL; res = res->ai_next) {
		switch (res->ai_family) {
		case PF_INET:
			assert(PF_INET < 256);
			sin4 = (void*)res->ai_addr;
			assert(sizeof(sin4->sin_addr) == 4);
			u = (void*)&sin4->sin_addr;
			if (ae->t_mask == NULL)
				ae->mask = 32;
			vcc_acl_add_entry(tl, ae, 4, u, res->ai_family);
			break;
		case PF_INET6:
			assert(PF_INET6 < 256);
			sin6 = (void*)res->ai_addr;
			assert(sizeof(sin6->sin6_addr) == 16);
			u = (void*)&sin6->sin6_addr;
			if (ae->t_mask == NULL)
				ae->mask = 128;
			vcc_acl_add_entry(tl, ae, 16, u, res->ai_family);
			break;
		default:
			continue;
		}
		if (tl->err)
			freeaddrinfo(res0);
		ERRCHK(tl);
	}
	freeaddrinfo(res0);

}

/*--------------------------------------------------------------------
 * Ancient stupidity on the part of X/Open and other standards orgs
 * dictate that "192.168" be translated to 192.0.0.168.  Ever since
 * CIDR happened, "192.168/16" notation has been used, but apparently
 * no API supports parsing this, so roll our own.
 */

static int
vcc_acl_try_netnotation(struct vcc *tl, struct acl_e *ae)
{
	unsigned char b[4];
	int i, j, k;
	unsigned u;
	const char *p;

	CHECK_OBJ_NOTNULL(ae, VCC_ACL_E_MAGIC);
	memset(b, 0, sizeof b);
	p = ae->addr;
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
	struct acl_e ae[1];
	char *sl, *e;

	INIT_OBJ(ae, VCC_ACL_E_MAGIC);

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
	ae->addr = ae->t_addr->dec;
	vcc_NextToken(tl);

	if (strchr(ae->t_addr->dec, '/') != NULL) {
		sl = strchr(ae->addr, '/');
		AN(sl);
		*sl++ = '\0';
		e = NULL;
		ae->mask = strtoul(sl, &e, 10);
		if (*e != '\0') {
			VSB_cat(tl->sb, ".../mask is not numeric.\n");
			vcc_ErrWhere(tl, ae->t_addr);
			return;
		}
		ae->t_mask = ae->t_addr;
		if (tl->t->tok == '/') {
			VSB_cat(tl->sb, "/mask only allowed once.\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
	} else if (tl->t->tok == '/') {
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
 * Emit the tokens making up an entry as C-strings
 */

static void
vcc_acl_emit_tokens(const struct vcc *tl, const struct acl_e *ae)
{
	struct token *t;
	const char *sep = "";

	CHECK_OBJ_NOTNULL(ae, VCC_ACL_E_MAGIC);
	t = ae->t_addr;
	do {
		if (t->tok == CSTR) {
			Fh(tl, 0, "%s\"\\\"\" ", sep);
			EncToken(tl->fh, t);
			Fh(tl, 0, " \"\\\"\"");
		} else if (t == ae->t_mask) {
			Fh(tl, 0, " \"%u\"", ae->mask - 8);
		} else {
			Fh(tl, 0, "%s\"%.*s\"", sep, PF(t));
		}
		if (t == ae->t_mask)
			break;
		t = vcc_PeekTokenFrom(tl, t);
		AN(t);
		sep = " ";
	} while (ae->t_mask != NULL);
	if (ae->fixed)
		Fh(tl, 0, "\" fixed: %s\"", ae->fixed);
}

/*********************************************************************
 * Emit ACL on table format
 */

static unsigned
vcc_acl_emit_tables(const struct vcc *tl, unsigned n, const char *name)
{
	struct acl_e *ae;
	unsigned rv = sizeof(ae->data) + 3;
	unsigned nn = 0;
	size_t sz;

	Fh(tl, 0, "\nstatic unsigned char acl_tbl_%s[%u*%u] = {\n",
	    name, n, rv);
	VRBT_FOREACH(ae, acl_tree, &tl->acl->acl_tree) {
		if (ae->overlapped)
			continue;
		Fh(tl, 0, "\t0x%02x,", ae->not ? 0 : 1);
		Fh(tl, 0, "0x%02x,", (ae->mask >> 3) - 1);
		Fh(tl, 0, "0x%02x,", (0xff00 >> (ae->mask & 7)) & 0xff);
		for (sz = 0; sz < sizeof(ae->data); sz++)
			Fh(tl, 0, "0x%02x,", ae->data[sz]);
		for (; sz < rv - 3; sz++)
			Fh(tl, 0, "0,");
		Fh(tl, 0, "\n");
		nn++;
	}
	assert(n == nn);
	Fh(tl, 0, "};\n");
	if (tl->acl->flag_log) {
		Fh(tl, 0, "\nstatic const char *acl_str_%s[%d] = {\n",
		    name, n);
		VRBT_FOREACH(ae, acl_tree, &tl->acl->acl_tree) {
			if (ae->overlapped)
				continue;
			Fh(tl, 0, "\t");
			Fh(tl, 0, "\"%sMATCH %s \" ",
			    ae->not ? "NEG_" : "", name);
			vcc_acl_emit_tokens(tl, ae);
			Fh(tl, 0, ",\n");
		}
		Fh(tl, 0, "};\n");
	}
	return (rv);
}

/*********************************************************************
 * Emit a function to match the ACL we have collected
 */

static void
vcc_acl_emit(struct vcc *tl, const struct symbol *sym)
{
	struct acl_e *ae, *ae2;
	int depth, l, m, i;
	unsigned at[ACL_MAXADDR];
	struct inifin *ifp = NULL;
	struct vsb *func;
	unsigned n, no, nw = 0;

	func = VSB_new_auto();
	AN(func);
	VSB_cat(func, "match_acl_");
	VCC_PrintCName(func, sym->name, NULL);
	AZ(VSB_finish(func));

	depth = -1;
	at[0] = 256;
	ae2 = NULL;
	n = no = 0;
	VRBT_FOREACH_REVERSE(ae, acl_tree, &tl->acl->acl_tree) {
		n++;
		if (ae2 == NULL) {
			ae2 = ae;
		} else if (vcl_acl_disjoint(ae, ae2)) {
			ae2 = ae;
		} else {
			no++;
			ae->overlapped = 1;
		}
	}

	Fh(tl, 0, "/* acl_n_%s n %u no %u */\n", sym->name, n, no);
	if (n - no < (1<<1))
		no = n;
	else if (!tl->acl->flag_table)
		no = n;

	if (no < n)
		nw = vcc_acl_emit_tables(tl, n - no, sym->name);


	Fh(tl, 0, "\nstatic int v_matchproto_(acl_match_f)\n");
	Fh(tl, 0, "%s(VRT_CTX, const VCL_IP p)\n", VSB_data(func));
	Fh(tl, 0, "{\n");
	Fh(tl, 0, "\tconst unsigned char *a;\n");
	Fh(tl, 0, "\tint fam;\n");
	Fh(tl, 0, "\n");
	Fh(tl, 0, "\tfam = VRT_VSA_GetPtr(ctx, p, &a);\n");
	Fh(tl, 0, "\tif (fam < 0) {\n");
	Fh(tl, 0, "\t\tVRT_fail(ctx,");
	Fh(tl, 0, " \"ACL %s: no protocol family\");\n", sym->name);
	Fh(tl, 0, "\t\treturn(0);\n");
	Fh(tl, 0, "\t}\n\n");
	if (!tl->err_unref) {
		ifp = New_IniFin(tl);
		VSB_printf(ifp->ini,
			"\t(void)%s;\n", VSB_data(func));
	}

	VRBT_FOREACH(ae, acl_tree, &tl->acl->acl_tree) {

		if (no < n && !ae->overlapped)
			continue;

		/* Find how much common prefix we have */
		for (l = 0; l <= depth && l * 8 < (int)ae->mask - 7; l++) {
			assert(l >= 0);
			if (ae->data[l] != at[l])
				break;
		}

		/* Back down, if necessary */
		while (l <= depth) {
			Fh(tl, 0, "\t%*s}\n", -depth, "");
			depth--;
		}

		m = (int)ae->mask;
		assert(m >= l*8);
		m -= l * 8;

		/* Do whole byte compares */
		for (i = l; m >= 8; m -= 8, i++) {
			if (i == 0)
				Fh(tl, 0, "\t%*s%sif (fam == %d) {\n",
				    -i, "", "", ae->data[i]);
			else
				Fh(tl, 0, "\t%*s%sif (a[%d] == %d) {\n",
				    -i, "", "", i - 1, ae->data[i]);
			at[i] = ae->data[i];
			depth = i;
		}

		if (m > 0) {
			// XXX can remove masking due to fixup
			/* Do fractional byte compares */
			Fh(tl, 0, "\t%*s%sif ((a[%d] & 0x%x) == %d) {\n",
			    -i, "", "", i - 1, (0xff00 >> m) & 0xff,
			    ae->data[i] & ((0xff00 >> m) & 0xff));
			at[i] = 256;
			depth = i;
		}

		i = ((int)ae->mask + 7) / 8;

		if (tl->acl->flag_log) {
			Fh(tl, 0, "\t%*sVPI_acl_log(ctx, \"%sMATCH %s \" ",
			    -i, "", ae->not ? "NEG_" : "", sym->name);
			vcc_acl_emit_tokens(tl, ae);
			Fh(tl, 0, ");\n");
		}

		Fh(tl, 0, "\t%*sreturn (%d);\n", -i, "", ae->not ? 0 : 1);
	}

	/* Unwind */
	for (; 0 <= depth; depth--)
		Fh(tl, 0, "\t%*.*s}\n", depth, depth, "");

	if (no < n) {
		Fh(tl, 0, "\treturn(\n\t    VPI_acl_table(ctx,\n");
		Fh(tl, 0, "\t\tp,\n");
		Fh(tl, 0, "\t\t%u, %u,\n", n - no, nw);
		Fh(tl, 0, "\t\tacl_tbl_%s,\n", sym->name);
		if (tl->acl->flag_log)
			Fh(tl, 0, "\t\tacl_str_%s,\n", sym->name);
		else
			Fh(tl, 0, "\t\tNULL,\n");
		Fh(tl, 0, "\t\t\"NO MATCH %s\"\n\t    )\n\t);\n", sym->name);
	} else {
		/* Deny by default */
		if (tl->acl->flag_log)
			Fh(tl, 0, "\tVPI_acl_log(ctx, \"NO_MATCH %s\");\n",
			    sym->name);
		Fh(tl, 0, "\treturn(0);\n");
	}
	Fh(tl, 0, "}\n");

	/* Emit the struct that will be referenced */
	Fh(tl, 0, "\nstatic const struct vrt_acl %s[] = {{\n", sym->rname);
	Fh(tl, 0, "\t.magic = VRT_ACL_MAGIC,\n");
	Fh(tl, 0, "\t.match = &%s,\n", VSB_data(func));
	Fh(tl, 0, "\t.name = \"%s\",\n", sym->name);
	Fh(tl, 0, "}};\n\n");
	if (!tl->err_unref) {
		AN(ifp);
		VSB_printf(ifp->ini, "\t(void)%s;", sym->rname);
	}
	VSB_destroy(&func);
}

void
vcc_ParseAcl(struct vcc *tl)
{
	struct symbol *sym;
	int sign;
	struct acl acl[1];

	INIT_OBJ(acl, VCC_ACL_MAGIC);
	tl->acl = acl;
	acl->flag_pedantic = 1;
	vcc_NextToken(tl);
	VRBT_INIT(&acl->acl_tree);

	vcc_ExpectVid(tl, "ACL");
	ERRCHK(tl);
	sym = VCC_HandleSymbol(tl, ACL);
	ERRCHK(tl);
	AN(sym);

	while (1) {
		sign = vcc_IsFlag(tl);
		if (tl->err) {
			VSB_cat(tl->sb,
			    "Valid ACL flags are `log` and `table`:\n");
			return;
		}
		if (sign < 0)
			break;
		if (vcc_IdIs(tl->t, "log")) {
			acl->flag_log = sign;
			vcc_NextToken(tl);
		} else if (vcc_IdIs(tl->t, "fold")) {
			acl->flag_fold = sign;
			vcc_NextToken(tl);
		} else if (vcc_IdIs(tl->t, "pedantic")) {
			acl->flag_pedantic = sign;
			vcc_NextToken(tl);
		} else if (vcc_IdIs(tl->t, "table")) {
			acl->flag_table = sign;
			vcc_NextToken(tl);
		} else {
			VSB_cat(tl->sb, "Unknown ACL flag:\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
	}

	SkipToken(tl, '{');

	while (tl->t->tok != '}') {
		vcc_acl_entry(tl);
		ERRCHK(tl);
		SkipToken(tl, ';');
	}
	SkipToken(tl, '}');

	vcc_acl_emit(tl, sym);
}
