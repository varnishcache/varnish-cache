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
 */

#include <stdint.h>

#include "common/common.h"

struct cli;
struct parspec;

extern struct vev_base	*mgt_evb;
extern unsigned		d_flag;
extern int		exit_status;

/* mgt_child.c */
extern pid_t child_pid;
void MGT_Run(void);
void mgt_stop_child(void);
void mgt_got_fd(int fd);
void MGT_Child_Cli_Fail(void);
int MGT_open_sockets(void);
void MGT_close_sockets(void);

/* mgt_cli.c */

typedef void mgt_cli_close_f(void *priv);
void mgt_cli_setup(int fdi, int fdo, int verbose, const char *ident,
    mgt_cli_close_f *close_func, void *priv);
int mgt_cli_askchild(unsigned *status, char **resp, const char *fmt, ...)
    __printflike(3, 4);
void mgt_cli_start_child(int fdi, int fdo);
void mgt_cli_stop_child(void);
void mgt_cli_telnet(const char *T_arg);
void mgt_cli_master(const char *M_arg);
void mgt_cli_secret(const char *S_arg);
void mgt_cli_close_all(void);

/* mgt_main.c */
extern struct VSC_C_mgt	*VSC_C_mgt;
extern struct VSC_C_mgt static_VSC_C_mgt;
struct choice {
	const char      *name;
	const void	*ptr;
};
const void *pick(const struct choice *cp, const char *which, const char *kind);

/* mgt_param.c */
void MCF_InitParams(struct cli *);
void MCF_CollectParams(void);
void MCF_SetDefault(const char *param, const char *def);
void MCF_SetMinimum(const char *param, const char *def);
void MCF_SetMaximum(const char *param, const char *def);
void MCF_ParamSet(struct cli *, const char *param, const char *val);
void MCF_ParamProtect(struct cli *, const char *arg);
void MCF_DumpRstParam(void);
void MCF_AddParams(struct parspec *ps);
extern struct params mgt_param;

/* mgt_param_tcp.c */
void MCF_TcpParams(void);

/* mgt_sandbox.c */
enum sandbox_e {
	SANDBOX_VCC = 1,
	SANDBOX_CC = 2,
	SANDBOX_VCLLOAD = 3,
	SANDBOX_WORKER = 4,
};

typedef void mgt_sandbox_f(enum sandbox_e);
extern mgt_sandbox_f *mgt_sandbox;

/* mgt_sandbox_solaris.c */
#ifdef HAVE_SETPPRIV
mgt_sandbox_f mgt_sandbox_solaris;
#endif

/* mgt_shmem.c */
void mgt_SHM_Init(void);
void mgt_SHM_static_alloc(const void *, ssize_t size,
    const char *class, const char *type, const char *ident);
void mgt_SHM_Create(void);
void mgt_SHM_Destroy(int keep);
void mgt_SHM_Size_Adjust(void);


/* stevedore_mgt.c */
void STV_Config(const char *spec);
void STV_Config_Transient(void);

/* mgt_vcc.c */
void mgt_vcc_init(void);
int mgt_vcc_default(const char *bflag, const char *f_arg, char *vcl, int Cflag);
int mgt_push_vcls_and_start(unsigned *status, char **p);
int mgt_has_vcl(void);
extern char *mgt_cc_cmd;
extern const char *mgt_vcl_dir;
extern const char *mgt_vmod_dir;
extern unsigned mgt_vcc_err_unref;
extern unsigned mgt_vcc_allow_inline_c;
extern unsigned mgt_vcc_unsafe_path;

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

#if defined(PTHREAD_CANCELED) || defined(PTHREAD_MUTEX_DEFAULT)
#error "Keep pthreads out of in manager process"
#endif
