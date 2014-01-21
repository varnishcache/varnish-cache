/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "vcc_compile.h"

#include "vre.h"
#include "vrt.h"
#include "vsa.h"

/*--------------------------------------------------------------------*/

const char *
vcc_regexp(struct vcc *tl)
{
	char buf[BUFSIZ], *p;
	vre_t *t;
	const char *error;
	int erroroffset;
	struct inifin *ifp;

	Expect(tl, CSTR);
	if (tl->err)
		return (NULL);
	memset(&t, 0, sizeof t);
	t = VRE_compile(tl->t->dec, 0, &error, &erroroffset);
	if (t == NULL) {
		VSB_printf(tl->sb,
		    "Regexp compilation error:\n\n%s\n\n", error);
		vcc_ErrWhere(tl, tl->t);
		return (NULL);
	}
	VRE_free(&t);
	sprintf(buf, "VGC_re_%u", tl->unique++);
	p = TlAlloc(tl, strlen(buf) + 1);
	strcpy(p, buf);

	Fh(tl, 0, "static void *%s;\n", buf);
	ifp = New_IniFin(tl);
	VSB_printf(ifp->ini, "\tVRT_re_init(&%s, ",buf);
	EncToken(ifp->ini, tl->t);
	VSB_printf(ifp->ini, ");");
	VSB_printf(ifp->fin, "\tVRT_re_fini(%s);", buf);
	return (p);
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

static const char *
vcc_sockaddr(struct vcc *tl, const void *sa, unsigned sal)
{
	const int sz = sizeof(unsigned long long);
	const unsigned n = (vsa_suckaddr_len + sz - 1) / sz;
	unsigned len;
	unsigned long long b[n];
	struct suckaddr *sua;
	char *p;

	AN(sa);
	AN(sal);

	sua = VSA_Malloc(sa, sal);
	AN(sua);

	Fh(tl, 0, "static const unsigned long long");
	Fh(tl, 0, " sockaddr_%u[%d] = {\n", tl->unique, n);
	memcpy(b, sua, vsa_suckaddr_len);
	free(sua);
	for (len = 0; len < n; len++)
		Fh(tl, 0, "%s    0x%0*llxLL",
		    len ? ",\n" : "", sz * 2, b[len]);
	Fh(tl, 0, "\n};\n");

	p = TlAlloc(tl, 40);
	AN(p);
	sprintf(p, "(const void*)sockaddr_%u", tl->unique);

	tl->unique++;
	return (p);
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

struct foo_proto {
	const char		*name;
	int			family;
	struct sockaddr_storage	sa;
	socklen_t		l;
	const char		**dst;
	const char		**dst_ascii;
};

void
Resolve_Sockaddr(struct vcc *tl,
    const char *host,
    const char *port,
    const char **ipv4,
    const char **ipv4_ascii,
    const char **ipv6,
    const char **ipv6_ascii,
    const char **p_ascii,
    int maxips,
    const struct token *t_err,
    const char *errid)
{
	struct foo_proto protos[3], *pp;
	struct addrinfo *res, *res0, *res1, hint;
	int error, retval;
	char hbuf[NI_MAXHOST];

	memset(protos, 0, sizeof protos);
	protos[0].name = "ipv4";
	protos[0].family = PF_INET;
	protos[0].dst = ipv4;
	protos[0].dst_ascii = ipv4_ascii;
	*ipv4 = NULL;

	protos[1].name = "ipv6";
	protos[1].family = PF_INET6;
	protos[1].dst = ipv6;
	protos[1].dst_ascii = ipv6_ascii;
	*ipv6 = NULL;

	retval = 0;
	memset(&hint, 0, sizeof hint);
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;

	error = getaddrinfo(host, port, &hint, &res0);
	if (error) {
		VSB_printf(tl->sb,
		    "%s '%.*s' could not be resolved to an IP address:\n",
		    errid, PF(t_err));
		VSB_printf(tl->sb,
		    "\t%s\n"
		    "(Sorry if that error message is gibberish.)\n",
		    gai_strerror(error));
		vcc_ErrWhere(tl, t_err);
		return;
	}

	for (res = res0; res; res = res->ai_next) {
		for (pp = protos; pp->name != NULL; pp++)
			if (res->ai_family == pp->family)
				break;
		if (pp->name == NULL) {
			/* Unknown proto, ignore */
			continue;
		}
		if (pp->l == res->ai_addrlen &&
		    !memcmp(&pp->sa, res->ai_addr, pp->l)) {
			/*
			 * Same address we already emitted.
			 * This can happen using /etc/hosts
			 */
			continue;
		}

		if (pp->l != 0 || retval == maxips) {
			VSB_printf(tl->sb,
			    "%s %.*s: resolves to too many addresses.\n"
			    "Only one IPv4 %s IPv6 are allowed.\n"
			    "Please specify which exact address "
			    "you want to use, we found all of these:\n",
			    errid, PF(t_err),
			    maxips > 1 ? "and one" :  "or");
			for (res1 = res0; res1 != NULL; res1 = res1->ai_next) {
				error = getnameinfo(res1->ai_addr,
				    res1->ai_addrlen, hbuf, sizeof hbuf,
				    NULL, 0, NI_NUMERICHOST);
				AZ(error);
				VSB_printf(tl->sb, "\t%s\n", hbuf);
			}
			freeaddrinfo(res0);
			vcc_ErrWhere(tl, t_err);
			return;
		}

		pp->l =  res->ai_addrlen;
		assert(pp->l <= sizeof(struct sockaddr_storage));
		memcpy(&pp->sa, res->ai_addr, pp->l);

		error = getnameinfo(res->ai_addr, res->ai_addrlen,
		    hbuf, sizeof hbuf, NULL, 0, NI_NUMERICHOST);
		AZ(error);

		Fh(tl, 0, "\n/* \"%s\" -> %s */\n", host, hbuf);
		*(pp->dst) = vcc_sockaddr(tl, &pp->sa, pp->l);
		if (pp->dst_ascii != NULL)
			*pp->dst_ascii = TlDup(tl, hbuf);
		retval++;
	}
	if (p_ascii != NULL) {
		error = getnameinfo(res0->ai_addr,
		    res0->ai_addrlen, NULL, 0, hbuf, sizeof hbuf,
		    NI_NUMERICSERV);
		AZ(error);
		*p_ascii = TlDup(tl, hbuf);
	}
	if (retval == 0) {
		VSB_printf(tl->sb,
		    "%s '%.*s': resolves to "
		    "neither IPv4 nor IPv6 addresses.\n",
		    errid, PF(t_err) );
		vcc_ErrWhere(tl, t_err);
	}
}
