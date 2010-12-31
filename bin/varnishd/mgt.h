/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2010 Linpro AS
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

#include <stdint.h>

#include "vqueue.h"

#include "common.h"
#include "miniobj.h"

#include "libvarnish.h"

struct cli;

extern struct vev_base	*mgt_evb;
extern unsigned		d_flag;
extern int		exit_status;

/* mgt_child.c */
extern pid_t child_pid;
void MGT_Run(void);
void mgt_stop_child(void);
void mgt_got_fd(int fd);
void MGT_Child_Cli_Fail(void);

/* mgt_cli.c */

typedef void mgt_cli_close_f(void *priv);
void mgt_cli_setup(int fdi, int fdo, int verbose, const char *ident, mgt_cli_close_f *close_func, void *priv);
int mgt_cli_askchild(unsigned *status, char **resp, const char *fmt, ...);
void mgt_cli_start_child(int fdi, int fdo);
void mgt_cli_stop_child(void);
void mgt_cli_telnet(const char *T_arg);
void mgt_cli_master(const char *M_arg);
void mgt_cli_secret(const char *S_arg);
void mgt_cli_close_all(void);

/* mgt_param.c */
void MCF_ParamSync(void);
void MCF_ParamInit(struct cli *);
void MCF_ParamSet(struct cli *, const char *param, const char *val);
#ifdef DIAGNOSTICS
void MCF_DumpMdoc(void);
#endif

/* mgt_shmem.c */
void mgt_SHM_Init(const char *arg);
void mgt_SHM_Pid(void);

/* mgt_vcc.c */
void mgt_vcc_init(void);
int mgt_vcc_default(const char *bflag, const char *f_arg, char *vcl, int Cflag);
int mgt_push_vcls_and_start(unsigned *status, char **p);
int mgt_has_vcl(void);
extern char *mgt_cc_cmd;
extern char *mgt_vcl_dir;
extern char *mgt_vmod_dir;
extern unsigned mgt_vcc_err_unref;

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
