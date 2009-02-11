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
 */

#include "common.h"
#include "miniobj.h"

#include "libvarnish.h"

struct cli;

extern struct vev_base	*mgt_evb;

/* mgt_child.c */
void mgt_run(int dflag, const char *T_arg);
extern pid_t mgt_pid, child_pid;
void mgt_stop_child(void);

/* mgt_cli.c */

void mgt_cli_setup(int fdi, int fdo, int verbose, const char *ident);
int mgt_cli_askchild(unsigned *status, char **resp, const char *fmt, ...);
void mgt_cli_start_child(int fdi, int fdo);
void mgt_cli_stop_child(void);
void mgt_cli_telnet(int dflag, const char *T_arg);

/* mgt_param.c */
void MCF_ParamSync(void);
void MCF_ParamInit(struct cli *);
void MCF_ParamSet(struct cli *, const char *param, const char *val);

/* mgt_vcc.c */
void mgt_vcc_init(void);
int mgt_vcc_default(const char *bflag, char *vcl, int Cflag);
int mgt_push_vcls_and_start(unsigned *status, char **p);
int mgt_has_vcl(void);
extern char *mgt_cc_cmd;


#define REPORT0(pri, fmt)				\
	do {						\
		fprintf(stderr, fmt "\n");		\
		syslog(pri, fmt);			\
	} while (0)

#define REPORT(pri, fmt, ...)				\
	do {						\
		fprintf(stderr, fmt "\n", __VA_ARGS__);	\
		syslog(pri, fmt, __VA_ARGS__);		\
	} while (0)

