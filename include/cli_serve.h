/*-
 * Copyright (c) 2010-2011 Varnish Software AS
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
 */

struct VCLS;
typedef void cls_cb_f(void *priv);
typedef void cls_cbc_f(const struct cli*);
struct VCLS *VCLS_New(cls_cbc_f *before, cls_cbc_f *after, unsigned maxlen);
struct cli *VCLS_AddFd(struct VCLS *cs, int fdi, int fdo, cls_cb_f *closefunc,
    void *priv);
int VCLS_AddFunc(struct VCLS *cs, unsigned auth, struct cli_proto *clp);
int VCLS_Poll(struct VCLS *cs, int timeout);
int VCLS_PollFd(struct VCLS *cs, int fd, int timeout);
void VCLS_Destroy(struct VCLS **);

/* From libvarnish/cli.c */
cli_func_t	VCLS_func_close;
cli_func_t	VCLS_func_help;
cli_func_t	VCLS_func_ping;

