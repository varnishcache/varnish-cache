/*
 * Copyright (c) 2008-2009 Linpro AS
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
 * $Id$
 */

#include <limits.h>
#include <signal.h>
#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif

struct vsb;
struct vtclog;
struct cmds;

#define CMD_ARGS \
    char * const *av, void *priv, const struct cmds *cmd, struct vtclog *vl

typedef void cmd_f(CMD_ARGS);

struct cmds {
	const char	*name;
	cmd_f		*cmd;
};

void parse_string(char *buf, const struct cmds *cmd, void *priv,
    struct vtclog *vl);

cmd_f cmd_delay;
cmd_f cmd_server;
cmd_f cmd_client;
cmd_f cmd_varnish;
cmd_f cmd_sema;

extern volatile sig_atomic_t vtc_error; /* Error, bail out */
extern int vtc_stop;		/* Abandon current test, no error */
extern pthread_t	vtc_thread;

void init_sema(void);

void http_process(struct vtclog *vl, const char *spec, int sock, int sfd);

void cmd_server_genvcl(struct vsb *vsb);

void vtc_loginit(char *buf, unsigned buflen);
struct vtclog *vtc_logopen(const char *id);
void vtc_logclose(struct vtclog *vl);
void vtc_log(struct vtclog *vl, unsigned lvl, const char *fmt, ...);
void vtc_dump(struct vtclog *vl, unsigned lvl, const char *pfx,
    const char *str);

int exec_file(const char *fn, const char *script, const char *tmpdir,
    char *logbuf, unsigned loglen);

void macro_def(struct vtclog *vl, const char *instance, const char *name,
    const char *fmt, ...);
struct vsb *macro_expand(struct vtclog *vl, const char *text);
