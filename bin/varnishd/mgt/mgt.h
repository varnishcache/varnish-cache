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
    __v_printflike(3, 4);
void mgt_cli_start_child(int fdi, int fdo);
void mgt_cli_stop_child(void);
void mgt_cli_telnet(const char *T_arg);
void mgt_cli_master(const char *M_arg);
void mgt_cli_secret(const char *S_arg);
void mgt_cli_close_all(void);

/* mgt_jail.c */

enum jail_subproc_e {
	JAIL_SUBPROC_VCC,
	JAIL_SUBPROC_CC,
	JAIL_SUBPROC_VCLLOAD,
	JAIL_SUBPROC_WORKER,
};

enum jail_master_e {
	JAIL_MASTER_LOW,
	JAIL_MASTER_HIGH,
};

typedef int jail_init_f(char **);
typedef void jail_master_f(enum jail_master_e);
typedef void jail_subproc_f(enum jail_subproc_e);
typedef void jail_make_workdir_f(const char *dname);
typedef void jail_storage_file_f(int fd);

struct jail_tech {
	unsigned		magic;
#define JAIL_TECH_MAGIC		0x4d00fa4d
	const char		*name;
	jail_init_f		*init;
	jail_master_f		*master;
	jail_subproc_f		*subproc;
	jail_make_workdir_f	*make_workdir;
	jail_storage_file_f	*storage_file;
};

void VJ_Init(const char *j_arg);
void VJ_master(enum jail_master_e jme);
void VJ_subproc(enum jail_subproc_e jse);
void VJ_make_workdir(const char *dname);
void VJ_storage_file(int fd);

extern const struct jail_tech jail_tech_unix;
extern const struct jail_tech jail_tech_solaris;

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

/* mgt_shmem.c */
void mgt_SHM_Init(void);
void mgt_SHM_static_alloc(const void *, ssize_t size,
    const char *class, const char *type, const char *ident);
void mgt_SHM_Create(void);
void mgt_SHM_Commit(void);
void mgt_SHM_Destroy(int keep);
void mgt_SHM_Size_Adjust(void);


/* stevedore_mgt.c */
void STV_Config(const char *spec);
void STV_Config_Transient(void);

/* mgt_vcc.c */
void mgt_vcc_init(void);
void mgt_vcc_default(struct cli *, const char *b_arg, const char *vclsrc,
    int Cflag);
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
