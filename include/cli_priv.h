/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 *
 * Varnish process internal CLI stuff.
 *
 * XXX: at a latter date we may want to move some to cli.h/libvarnishapi
 */

#define CLI_PRIV_H

struct cli;	/* NB: struct cli is opaque at this level.  */

typedef void cli_func_t(struct cli*, const char * const *av, void *priv);

struct cli_proto {
	/* These must match the CLI_* macros in cli.h */
	const char		*request;
	const char		*syntax;
	const char		*help;
	unsigned		minarg;
	unsigned		maxarg;

	/* Dispatch information */
	cli_func_t		*func;
	void			*priv;
};

/* The implementation must provide these functions */
void cli_out(struct cli *cli, const char *fmt, ...);
void cli_param(struct cli *cli);
void cli_result(struct cli *cli, unsigned r);

/* From libvarnish/cli.c */
void cli_dispatch(struct cli *cli, struct cli_proto *clp, const char *line);
cli_func_t	cli_func_help;
cli_func_t	cli_func_ping;
struct cli_proto *cli_concat(struct cli_proto *, struct cli_proto *);
