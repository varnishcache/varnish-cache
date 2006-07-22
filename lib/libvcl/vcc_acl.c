/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdarg.h>
#include <sbuf.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vcc_priv.h"
#include "vcc_compile.h"

#include "libvcl.h"

unsigned
vcc_IpVal(struct tokenlist *tl)
{
	unsigned u, v;
	struct token *t;

	t = tl->t;
	u = UintVal(tl);
	if (u < 256) {
		v = u << 24;
		Expect(tl, '.');
		vcc_NextToken(tl);
		t = tl->t;
		u = UintVal(tl);
		if (u < 256) {
			v |= u << 16;
			Expect(tl, '.');
			vcc_NextToken(tl);
			t = tl->t;
			u = UintVal(tl);
			if (u < 256) {
				v |= u << 8;
				Expect(tl, '.');
				vcc_NextToken(tl);
				t = tl->t;
				u = UintVal(tl);
				if (u < 256) {
					v |= u;
					return (v);
				}
			}
		}
	}
	sbuf_printf(tl->sb, "Illegal octet in IP number\n");
	vcc_ErrWhere(tl, t);
	return (0);
}

void
vcc_Cond_Ip(struct var *vp, struct tokenlist *tl)
{
	unsigned u;

	switch (tl->t->tok) {
	case '~':
		vcc_NextToken(tl);
		ExpectErr(tl, ID);
		AddRef(tl, tl->t, R_ACL);
		Fc(tl, 1, "ip_match(%s, acl_%T)\n", vp->rname, tl->t);
		vcc_NextToken(tl);
		break;
	case T_EQ:
	case T_NEQ:
		Fc(tl, 1, "%s %T ", vp->rname, tl->t);
		vcc_NextToken(tl);
		u = vcc_IpVal(tl);
		Fc(tl, 0, "%uU /* %u.%u.%u.%u */\n", u,
		    (u >> 24) & 0xff, (u >> 16) & 0xff,
		    (u >> 8) & 0xff, (u) & 0xff);
		break;
	default:
		sbuf_printf(tl->sb, "Illegal condition ");
		vcc_ErrToken(tl, tl->t);
		sbuf_printf(tl->sb, " on IP number variable\n");
		sbuf_printf(tl->sb, "  only '==', '!=' and '~' are legal\n");
		vcc_ErrWhere(tl, tl->t);
		break;
	}
}

void
vcc_Acl(struct tokenlist *tl)
{
	unsigned u, m;

	vcc_NextToken(tl);

	ExpectErr(tl, ID);
	AddDef(tl, tl->t, R_ACL);
	Fh(tl, 0, "static struct vcl_acl acl_%T[];\n", tl->t);
	Fc(tl, 1, "static struct vcl_acl acl_%T[] = {\n", tl->t);
	vcc_NextToken(tl);

	tl->indent += INDENT;

	ExpectErr(tl, '{');
	vcc_NextToken(tl);

	while (tl->t->tok == CNUM) {
		u = vcc_IpVal(tl);
		if (tl->t->tok == '/') {
			vcc_NextToken(tl);
			ExpectErr(tl, CNUM);
			m = UintVal(tl);
		} else
			m = 32;
		ExpectErr(tl, ';');
		vcc_NextToken(tl);
		Fc(tl, 1, "{ %11uU, %3uU }, /* %u.%u.%u.%u/%u */\n",
		    u, m,
		    (u >> 24) & 0xff, (u >> 16) & 0xff,
		    (u >> 8) & 0xff, (u) & 0xff, m);
	}
	ExpectErr(tl, '}');
	Fc(tl, 1, "{ %11uU, %3uU }\n", 0, 0);

	tl->indent -= INDENT;

	Fc(tl, 1, "};\n\n");
	vcc_NextToken(tl);
}
