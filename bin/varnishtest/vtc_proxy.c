/*-
 * Copyright (c) 2015 Varnish Software AS
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

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <unistd.h>

#include "vtc.h"

#include "vend.h"
#include "vsa.h"
#include "vtcp.h"

static const char vpx1_sig[] = {'P', 'R', 'O', 'X', 'Y'};
static const char vpx2_sig[] = {
	'\r', '\n', '\r', '\n', '\0', '\r', '\n',
	'Q', 'U', 'I', 'T', '\n',
};

//lint -esym(750, PP2_*)
#define PP2_TYPE_ALPN           0x01
#define PP2_TYPE_AUTHORITY      0x02
#define PP2_TYPE_CRC32C         0x03
#define PP2_TYPE_NOOP           0x04
#define PP2_TYPE_UNIQUE_ID      0x05
#define PP2_TYPE_SSL            0x20
#define PP2_SUBTYPE_SSL_VERSION 0x21
#define PP2_SUBTYPE_SSL_CN      0x22
#define PP2_SUBTYPE_SSL_CIPHER  0x23
#define PP2_SUBTYPE_SSL_SIG_ALG 0x24
#define PP2_SUBTYPE_SSL_KEY_ALG 0x25
#define PP2_SUBTYPE_SSL_MAX     0x25
#define PP2_TYPE_NETNS          0x30

struct pp2_type {
	const char * name;
	uint8_t type;
};

/* sorted ! */
static const struct pp2_type pp2_types[] = {
	{"alpn",	PP2_TYPE_ALPN},
	{"authority",	PP2_TYPE_AUTHORITY},
	{"crc32c",	PP2_TYPE_CRC32C},
	{"netns",	PP2_TYPE_NETNS},
	{"noop",	PP2_TYPE_NOOP},
	{"unique_id",	PP2_TYPE_UNIQUE_ID}
};

static int
pp2cmp(const void *va, const void *vb)
{
	const struct pp2_type *a = va;
	const struct pp2_type *b = vb;
	return (strcmp(a->name, b->name));
}

void
vtc_proxy_tlv(struct vtclog *vl, struct vsb *vsb, const char *kva)
{
	struct pp2_type *pp2, needle;
	char *save = NULL, *kv;
	struct vsb *vsb2;
	const char *p;
	uint16_t le;
	ssize_t sz;

	kv = strdup(kva);
	AN(kv);

	p = strtok_r(kv, "=", &save);
	AN(p);
	if (p[0] == '0' && p[1] == 'x') {
		p += 2;
		vsb2 = vtc_hex_to_bin(vl, p);
		AN(vsb2);
		if (VSB_len(vsb2) != 1)
			vtc_fatal(vl, "tlv hex type has wrong length");
		VSB_bcat(vsb, VSB_data(vsb2), 1);
		VSB_destroy(&vsb2);
	}
	else {
		needle = (typeof(needle)){p, 0};
		pp2 = bsearch(&needle, pp2_types, sizeof pp2_types / sizeof pp2_types[0],
		    sizeof pp2_types[0], pp2cmp);
		if (pp2 == NULL)
			vtc_fatal(vl, "tlv type %s not found", p);
		VSB_putc(vsb, pp2->type);
	}

	p = strtok_r(NULL, "", &save);
	if (p == NULL)
		vtc_fatal(vl, "tlv value missing");
	if (p[0] == '0' && p[1] == 'x')
		vsb2 = vtc_hex_to_bin(vl, p + 2);
	else {
		vsb2 = VSB_new_auto();
		AN(vsb2);
		VSB_cat(vsb2, p);
		AZ(VSB_finish(vsb2));
	}
	AN(vsb2);
	free(kv);

	sz = VSB_len(vsb2);
	assert(sz >= 0);
	assert(sz <= UINT16_MAX);

	vbe16enc(&le, (uint16_t)sz);
	assert(sizeof(le) == 2);
	VSB_bcat(vsb, &le, 2);
	VSB_bcat(vsb, VSB_data(vsb2), sz);
	VSB_destroy(&vsb2);
}

static void
vpx_enc_addr(struct vsb *vsb, int proto, const struct suckaddr *s)
{
	const struct sockaddr_in *sin4;
	const struct sockaddr_in6 *sin6;
	socklen_t sl;

	if (proto == PF_INET6) {
		sin6 = VSA_Get_Sockaddr(s, &sl);	//lint !e826
		AN(sin6);
		assert(sl >= sizeof(*sin6));
		VSB_bcat(vsb, &sin6->sin6_addr, sizeof(sin6->sin6_addr));
	} else {
		sin4 = VSA_Get_Sockaddr(s, &sl);	//lint !e826
		AN(sin4);
		assert(sl >= sizeof(*sin4));
		VSB_bcat(vsb, &sin4->sin_addr, sizeof(sin4->sin_addr));
	}
}

static void
vpx_enc_port(struct vsb *vsb, const struct suckaddr *s)
{
	uint8_t b[2];

	vbe16enc(b, (uint16_t)VSA_Port(s));
	VSB_bcat(vsb, b, sizeof(b));
}

int
vtc_send_proxy(int fd, int version, const struct suckaddr *sac,
    const struct suckaddr *sas, struct vsb *tlv)
{
	struct vsb *vsb;
	char hc[VTCP_ADDRBUFSIZE];
	char pc[VTCP_PORTBUFSIZE];
	char hs[VTCP_ADDRBUFSIZE];
	char ps[VTCP_PORTBUFSIZE];
	uint16_t le, l;
	int i;
	int proto;

	AN(sac);
	AN(sas);

	assert(version == 1 || version == 2);
	vsb = VSB_new_auto();
	AN(vsb);

	proto = VSA_Get_Proto(sas);
	assert(proto == PF_INET6 || proto == PF_INET);

	if (tlv == NULL)
		l = 0;
	else
		l = VSB_len(tlv);

	assert(l <= UINT16_MAX - 0x24);

	if (version == 1) {
		VSB_bcat(vsb, vpx1_sig, sizeof(vpx1_sig));
		if (proto == PF_INET6)
			VSB_cat(vsb, " TCP6 ");
		else if (proto == PF_INET)
			VSB_cat(vsb, " TCP4 ");
		VTCP_name(sac, hc, sizeof(hc), pc, sizeof(pc));
		VTCP_name(sas, hs, sizeof(hs), ps, sizeof(ps));
		VSB_printf(vsb, "%s %s %s %s\r\n", hc, hs, pc, ps);
	} else if (version == 2) {
		VSB_bcat(vsb, vpx2_sig, sizeof(vpx2_sig));
		VSB_putc(vsb, 0x21);
		if (proto == PF_INET6) {
			VSB_putc(vsb, 0x21);
			l += 0x24;
		} else if (proto == PF_INET) {
			VSB_putc(vsb, 0x11);
			l += 0x0c;
		} else
			WRONG("proto");

		vbe16enc(&le, l);
		assert(sizeof(le) == 2);
		VSB_bcat(vsb, &le, 2);
		vpx_enc_addr(vsb, proto, sac);
		vpx_enc_addr(vsb, proto, sas);
		vpx_enc_port(vsb, sac);
		vpx_enc_port(vsb, sas);
	} else
		WRONG("Wrong proxy version");

	AZ(VSB_finish(vsb));
	i = VSB_tofile(vsb, fd);
	VSB_destroy(&vsb);
	if (i != 0 && tlv != NULL)
		VSB_destroy(&tlv);
	if (i != 0 || tlv == NULL)
		return (i);

	i = VSB_tofile(tlv, fd);
	VSB_destroy(&tlv);
	return (i);
}
