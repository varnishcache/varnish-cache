/*-
 * Copyright (c) 2015 Varnish Software AS
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
    const struct suckaddr *sas)
{
	struct vsb *vsb;
	char hc[VTCP_ADDRBUFSIZE];
	char pc[VTCP_PORTBUFSIZE];
	char hs[VTCP_ADDRBUFSIZE];
	char ps[VTCP_PORTBUFSIZE];
	int i, len;
	int proto;

	AN(sac);
	AN(sas);

	assert(version == 1 || version == 2);
	vsb = VSB_new_auto();
	AN(vsb);

	proto = VSA_Get_Proto(sas);
	assert(proto == PF_INET6 || proto == PF_INET);

	if (version == 1) {
		VSB_bcat(vsb, vpx1_sig, sizeof(vpx1_sig));
		if (proto == PF_INET6)
			VSB_printf(vsb, " TCP6 ");
		else if (proto == PF_INET)
			VSB_printf(vsb, " TCP4 ");
		VTCP_name(sac, hc, sizeof(hc), pc, sizeof(pc));
		VTCP_name(sas, hs, sizeof(hs), ps, sizeof(ps));
		VSB_printf(vsb, "%s %s %s %s\r\n", hc, hs, pc, ps);
	} else if (version == 2) {
		VSB_bcat(vsb, vpx2_sig, sizeof(vpx2_sig));
		VSB_putc(vsb, 0x21);
		if (proto == PF_INET6) {
			VSB_putc(vsb, 0x21);
			VSB_putc(vsb, 0x00);
			VSB_putc(vsb, 0x24);
		} else if (proto == PF_INET) {
			VSB_putc(vsb, 0x11);
			VSB_putc(vsb, 0x00);
			VSB_putc(vsb, 0x0c);
		}
		vpx_enc_addr(vsb, proto, sac);
		vpx_enc_addr(vsb, proto, sas);
		vpx_enc_port(vsb, sac);
		vpx_enc_port(vsb, sas);
	} else
		WRONG("Wrong proxy version");

	AZ(VSB_finish(vsb));
	len = VSB_len(vsb);
	i = write(fd, VSB_data(vsb), len);
	VSB_delete(vsb);
	return (i != len);
}
