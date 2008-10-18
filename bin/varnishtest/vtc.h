/*
 * Copyright (c) 2006-2008 Linpro AS
 * All rights reserved.
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

cmd_f cmd_dump;
cmd_f cmd_delay;
cmd_f cmd_server;
cmd_f cmd_client;
cmd_f cmd_varnish;
cmd_f cmd_sema;

extern const char *vtc_file;
extern char *vtc_desc;
extern int vtc_verbosity;

void init_sema(void);

void http_process(struct vtclog *vl, const char *spec, int sock, int client);

void cmd_server_genvcl(struct vsb *vsb);

struct vtclog *vtc_logopen(const char *id);
void vtc_logclose(struct vtclog *vl);
void vtc_log(struct vtclog *vl, unsigned lvl, const char *fmt, ...);
void vtc_dump(struct vtclog *vl, unsigned lvl, const char *pfx,
    const char *str);
