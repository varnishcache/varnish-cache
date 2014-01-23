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
 *
 * Parameters related to TCP keepalives are not universally available
 * as socket options, and probing for system-wide defaults more appropriate
 * than our own involves slightly too much grunt-work to be neglible
 * so we sequestrate that code here.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "common/params.h"

#include "vss.h"

#include "mgt/mgt_param.h"

#ifdef HAVE_TCP_KEEP

static struct parspec mgt_parspec_tcp_keep[] = {
	{ "tcp_keepalive_time", tweak_timeout, &mgt_param.tcp_keepalive_time,
		"1", "7200",
		"The number of seconds a connection needs to be idle before "
		"TCP begins sending out keep-alive probes.",
		EXPERIMENTAL,
		"", "seconds" },
	{ "tcp_keepalive_probes", tweak_uint, &mgt_param.tcp_keepalive_probes,
		"1", "100",
		"The maximum number of TCP keep-alive probes to send before "
		"giving up and killing the connection if no response is "
		"obtained from the other end.",
		EXPERIMENTAL,
		"", "probes" },
	{ "tcp_keepalive_intvl", tweak_timeout, &mgt_param.tcp_keepalive_intvl,
		"1", "100",
		"The number of seconds between TCP keep-alive probes.",
		EXPERIMENTAL,
		"", "seconds" },
	{ NULL, NULL, NULL }
};

static void
tcp_probe(int sock, int nam, const char *param, unsigned def)
{
	int i;
	socklen_t l;
	unsigned u;
	char buf[10];
	const char *p;

	l = sizeof u;
	i = getsockopt(sock, IPPROTO_TCP, nam, &u, &l);
	if (i < 0 || u == 0)
		u = def;
	bprintf(buf, "%u", u);
	p = strdup(buf);
	AN(p);
	MCF_SetDefault(param, p);
}

static void
tcp_keep_probes(void)
{
	int i, s;
	struct vss_addr **ta = NULL;

	/* Probe a dummy socket for default values */

	i = VSS_resolve(":0", NULL, &ta);
	if (i == 0)
		return;		// XXX: log
	assert (i > 0);
	s = VSS_listen(ta[0], 10);
	if (s >= 0) {
		tcp_probe(s, TCP_KEEPIDLE, "tcp_keepalive_time",	600);
		tcp_probe(s, TCP_KEEPCNT, "tcp_keepalive_probes",	5);
		tcp_probe(s, TCP_KEEPINTVL, "tcp_keepalive_intvl",	5);
		AZ(close(s));
	}
	free(ta);
}
#endif

void
MCF_TcpParams(void)
{
#ifdef HAVE_TCP_KEEP
	MCF_AddParams(mgt_parspec_tcp_keep);
	tcp_keep_probes();
#endif
}
