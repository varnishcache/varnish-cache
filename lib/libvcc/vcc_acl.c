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
#include <vsa.h>

#define ACL_MAXADDR	(sizeof(struct in6_addr) + 1)

struct acl_e {
	VRBT_ENTRY(acl_e)	branch;
	unsigned char		data[ACL_MAXADDR];
	unsigned		mask;
	unsigned		not;
	unsigned		para;
	char			*addr;
	char			*fixed;
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
vcl_acl_cmp(const struct acl_e *ae1, const struct acl_e *ae2)
{
	const unsigned char *p1, *p2;
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

VRBT_GENERATE_INSERT_COLOR(acl_tree, acl_e, branch, static)
VRBT_GENERATE_FIND(acl_tree, acl_e, branch, vcl_acl_cmp, static)
VRBT_GENERATE_INSERT(acl_tree, acl_e, branch, vcl_acl_cmp, static)
VRBT_GENERATE_MINMAX(acl_tree, acl_e, branch, static)
VRBT_GENERATE_NEXT(acl_tree, acl_e, branch, static)

static char *
vcc_acl_chk(struct vcc *tl, const struct acl_e *ae, const int l,
    unsigned char *p, int fam)
{
	const unsigned char *u;
	char h[VTCP_ADDRBUFSIZE];
	char t[VTCP_ADDRBUFSIZE + 10];
	char s[vsa_suckaddr_len];
	struct suckaddr *sa;
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
	VSB_cat(tl->sb, "Address/Netmask mismatch, ");
	if (tl->acl_pedantic != 0)
		VSB_printf(tl->sb, "need be %s\n", t);
	else
		VSB_printf(tl->sb, "changed to %s\n", t);
	vcc_ErrWhere(tl, ae->t_addr);
	if (tl->acl_pedantic == 0)
		vcc_Warn(tl);
	return (strdup(t));
}

static void
vcc_acl_add_entry(struct vcc *tl, const struct acl_e *ae, int l,
    unsigned char *u, int fam)
{
	struct acl_e *ae2, *aen;

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
	aen = TlAlloc(tl, sizeof *ae2);
	AN(aen);
	*aen = *ae;

	aen->fixed = vcc_acl_chk(tl, ae, l, u, fam);

	/* We treat family as part of address, it saves code */
	assert(fam <= 0xff);
	aen->data[0] = fam & 0xff;
	aen->mask += 8;

	assert(l + 1UL <= sizeof aen->data);
	memcpy(aen->data + 1L, u, l);

	ae2 = VRBT_FIND(acl_tree, &tl->acl_tree, aen);
	if (ae2 != NULL) {
		if (ae2->not != aen->not) {
			VSB_cat(tl->sb, "Conflicting ACL entries:\n");
			vcc_ErrWhere(tl, ae2->t_addr);
			VSB_cat(tl->sb, "vs:\n");
			vcc_ErrWhere(tl, aen->t_addr);
		}
		free(aen);
		return;
	}
	VRBT_INSERT(acl_tree, &tl->acl_tree, aen);
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
	struct acl_e *ae;
	char *sl, *e;

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
	ae->addr = strdup(ae->t_addr->dec);
	AN(ae->addr);
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
			free(ae);
			return;
		}
		ae->t_mask = ae->t_addr;
		if (tl->t->tok == '/') {
			VSB_cat(tl->sb, "/mask only allowed once.\n");
			vcc_ErrWhere(tl, tl->t);
			free(ae);
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

	t = ae->t_addr;
	do {
		if (t->tok == CSTR) {
			Fh(tl, 0, "%s\"\\\"\" ", sep);
			EncToken(tl->fh, t);
			Fh(tl, 0, " \"\\\"\"");
		} else {
			Fh(tl, 0, "%s\"%.*s\"", sep, PF(t));
		}
		if (t == ae->t_mask)
			break;
		t = VTAILQ_NEXT(t, list);
		AN(t);
		sep = " ";
	} while (ae->t_mask != NULL);
	if (ae->fixed)
		Fh(tl, 0, "\" fixed: %s\"", ae->fixed);
}

/*********************************************************************
 * Emit a function to match the ACL we have collected
 */

static void
vcc_acl_emit(struct vcc *tl, const char *name, const char *rname)
{
	struct acl_e *ae;
	int depth, l, m, i;
	unsigned at[ACL_MAXADDR];
	struct inifin *ifp = NULL;
	struct vsb *func;

	func = VSB_new_auto();
	AN(func);
	VSB_printf(func, "match_acl_");
	VCC_PrintCName(func, name, NULL);
	AZ(VSB_finish(func));

	Fh(tl, 0, "\nstatic int v_matchproto_(acl_match_f)\n");
	Fh(tl, 0, "%s(VRT_CTX, const VCL_IP p)\n", VSB_data(func));
	Fh(tl, 0, "{\n");
	Fh(tl, 0, "\tconst unsigned char *a;\n");
	Fh(tl, 0, "\tint fam;\n");
	Fh(tl, 0, "\n");
	Fh(tl, 0, "\tfam = VRT_VSA_GetPtr(ctx, p, &a);\n");
	Fh(tl, 0, "\tif (fam < 0) {\n");
	Fh(tl, 0, "\t\tVPI_acl_log(ctx, \"NO_FAM %s\");\n", name);
	Fh(tl, 0, "\t\treturn(0);\n");
	Fh(tl, 0, "\t}\n\n");
	if (!tl->err_unref) {
		ifp = New_IniFin(tl);
		VSB_printf(ifp->ini,
			"\tif (0) %s(0, 0);\n", VSB_data(func));
	}
	depth = -1;
	at[0] = 256;
	VRBT_FOREACH(ae, acl_tree, &tl->acl_tree) {

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

		Fh(tl, 0, "\t%*sVPI_acl_log(ctx, \"%sMATCH %s \" ",
		    -i, "", ae->not ? "NEG_" : "", name);
		vcc_acl_emit_tokens(tl, ae);
		Fh(tl, 0, ");\n");

		Fh(tl, 0, "\t%*sreturn (%d);\n", -i, "", ae->not ? 0 : 1);
	}

	/* Unwind */
	for (; 0 <= depth; depth--)
		Fh(tl, 0, "\t%*.*s}\n", depth, depth, "");

	/* Deny by default */
	Fh(tl, 0, "\tVPI_acl_log(ctx, \"NO_MATCH %s\");\n", name);
	Fh(tl, 0, "\treturn (0);\n}\n");

	/* Emit the struct that will be referenced */
	Fh(tl, 0, "\nstatic const struct vrt_acl %s[] = {{\n", rname);
	Fh(tl, 0, "\t.magic = VRT_ACL_MAGIC,\n");
	Fh(tl, 0, "\t.match = &%s,\n", VSB_data(func));
	Fh(tl, 0, "\t.name = \"%s\",\n", name);
	Fh(tl, 0, "}};\n\n");
	if (!tl->err_unref) {
		AN(ifp);
		VSB_printf(ifp->ini, "\t(void)%s;\n", rname);
	}
	VSB_destroy(&func);
}

void
vcc_ParseAcl(struct vcc *tl)
{
	struct symbol *sym;

	vcc_NextToken(tl);
	VRBT_INIT(&tl->acl_tree);

	vcc_ExpectVid(tl, "ACL");
	ERRCHK(tl);
	sym = VCC_HandleSymbol(tl, ACL, ACL_SYMBOL_PREFIX);
	ERRCHK(tl);
	AN(sym);

	SkipToken(tl, '{');

	while (tl->t->tok != '}') {
		vcc_acl_entry(tl);
		ERRCHK(tl);
		SkipToken(tl, ';');
	}
	SkipToken(tl, '}');

	vcc_acl_emit(tl, sym->name, sym->rname);
}
