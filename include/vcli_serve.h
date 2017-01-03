/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 * Varnish process internal CLI stuff.
 *
 * XXX: at a latter date we may want to move some to cli.h/libvarnishapi
 */

#include "vcli.h"

struct cli;	/* NB: struct cli is opaque at this level.  */
struct vlu;
struct VCLS;

typedef void cli_func_t(struct cli*, const char * const *av, void *priv);

struct cli_cmd_desc {
	/* Must match CLI_CMD macro in include/tbl/cli_cmds.h */
	const char			*request;
	const char			*syntax;
	const char			*help;
	const char			*doc;
	int				minarg;
	int				maxarg;
};

#define CLI_CMD(U,l,s,h,d,m,M) extern const struct cli_cmd_desc CLICMD_##U[1];
#include "tbl/cli_cmds.h"

/* A CLI command */
struct cli_proto {
	const struct cli_cmd_desc	*desc;
	const char			*flags;

	/* Dispatch information */
	cli_func_t			*func;
	cli_func_t			*jsonfunc;
	void				*priv;

	unsigned			auth;
	VTAILQ_ENTRY(cli_proto)		list;
};

/* a CLI session */
struct cli {
	unsigned                magic;
#define CLI_MAGIC		0x4038d570
	struct vsb              *sb;
	enum VCLI_status_e      result;
	char                    *cmd;
	unsigned                auth;
	char                    challenge[34];
	char                    *ident;
	struct vlu              *vlu;
	struct VCLS             *cls;
	volatile unsigned       *limit;
};

/* The implementation must provide these functions */
int VCLI_Overflow(struct cli *cli);
void VCLI_Out(struct cli *cli, const char *fmt, ...) __v_printflike(2, 3);
void VCLI_Quote(struct cli *cli, const char *str);
void VCLI_JSON_str(struct cli *cli, const char *str);
void VCLI_JSON_ver(struct cli *cli, unsigned ver, const char * const * av);
void VCLI_SetResult(struct cli *cli, unsigned r);

typedef void cls_cb_f(void *priv);
typedef void cls_cbc_f(const struct cli*);
struct VCLS *VCLS_New(cls_cbc_f *before, cls_cbc_f *after,
    volatile unsigned *maxlen, volatile unsigned *limit);
struct cli *VCLS_AddFd(struct VCLS *cs, int fdi, int fdo, cls_cb_f *closefunc,
    void *priv);
void VCLS_AddFunc(struct VCLS *cs, unsigned auth, struct cli_proto *clp);
int VCLS_Poll(struct VCLS *cs, int timeout);
int VCLS_PollFd(struct VCLS *cs, int fd, int timeout);
void VCLS_Destroy(struct VCLS **);
void VCLS_Clone(struct VCLS *cs, struct VCLS *cso);

/* From libvarnish/cli.c */
cli_func_t	VCLS_func_close;
cli_func_t	VCLS_func_help;
cli_func_t	VCLS_func_help_json;
cli_func_t	VCLS_func_ping;
