/*-
 * Copyright (c) 2008-2015 Varnish Software AS
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

#define MAX_HDR		64

struct vtc_sess {
	unsigned		magic;
#define VTC_SESS_MAGIC		0x932bd565
	struct vtclog		*vl;
	char			*name;
	int			repeat;
	int			keepalive;
	int			fd;

	ssize_t			rcvbuf;
};

struct h2_window {
        uint64_t init;
        int64_t  size;
};

struct http {
	unsigned		magic;
#define HTTP_MAGIC		0x2f02169c
	int			*sfd;
	struct vtc_sess		*sess;
	vtim_dur		timeout;
	struct vtclog		*vl;

	struct vsb		*vsb;

	int			rcvbuf;
	int			nrxbuf;
	char			*rx_b;
	char			*rx_p;
	char			*rx_e;
	char			*rem_ip;
	char			*rem_port;
	char			*rem_path;
	char			*body;
	long			bodyl;
	char			bodylen[20];
	char			chunklen[20];

	char			*req[MAX_HDR];
	char			*resp[MAX_HDR];

	int			gziplevel;
	int			gzipresidual;

	int			head_method;

	int			fatal;

	/* H/2 */
	unsigned		h2;
	int			wf;

	pthread_t		tp;
	VTAILQ_HEAD(, stream)   streams;
	pthread_mutex_t		mtx;
	pthread_cond_t          cond;
	struct hpk_ctx		*encctx;
	struct hpk_ctx		*decctx;
	struct h2_window	h2_win_self[1];
	struct h2_window	h2_win_peer[1];
};

int http_process(struct vtclog *vl, struct vtc_sess *vsp, const char *spec,
    int sock, int *sfd, const char *addr, int rcvbuf);

