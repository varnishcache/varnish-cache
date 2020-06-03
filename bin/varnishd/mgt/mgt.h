/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 *
 */

#ifdef MGT_MGT_H
#error "Multiple includes of mgt/mgt.h"
#endif
#define MGT_MGT_H


#include <stdint.h>

#include <sys/types.h>

#include "vdef.h"

#include "miniobj.h"
#include "vas.h"
#include "vcs.h"
#include "vqueue.h"
#include "vsb.h"

#include "common/common_param.h"

struct vjsn;
struct vsc_seg;
struct vsmw_cluster;
#include "VSC_mgt.h"

struct cli;
struct parspec;
struct vcc;
struct vclprog;

extern struct vev_root	*mgt_evb;
extern unsigned		d_flag;
extern int		exit_status;

/* builtin_vcl.c */

extern const char * const builtin_vcl;

/* mgt_acceptor.c */

void MAC_Arg(const char *);
int MAC_reopen_sockets(void);

/* mgt_child.c */
void MCH_Init(void);
int MCH_Running(void);
void MCH_Stop_Child(void);
int MCH_Start_Child(void);
void MCH_TrackHighFd(int fd);
void MCH_Cli_Fail(void);

/* mgt_cli.c */
extern struct VCLS	*mgt_cls;

typedef int mgt_cli_close_f(void *priv);
void mgt_cli_setup(int fdi, int fdo, int auth, const char *ident,
    mgt_cli_close_f *close_func, void *priv);
int mgt_cli_askchild(unsigned *status, char **resp, const char *fmt, ...)
    v_printflike_(3, 4);
void mgt_cli_start_child(int fdi, int fdo);
void mgt_cli_stop_child(void);
void mgt_cli_telnet(const char *T_arg);
void mgt_cli_master(const char *M_arg);
void mgt_cli_secret(const char *S_arg);
void mgt_cli_close_all(void);
void mgt_DumpRstCli(void);
void mgt_cli_init_cls(void);
#define MCF_NOAUTH	0	/* NB: zero disables here-documents */
#define MCF_AUTH	16

/* mgt_jail.c */

enum jail_master_e {
	JAIL_MASTER_LOW = 0,
	JAIL_MASTER_SYSTEM,
	JAIL_MASTER_FILE,
	JAIL_MASTER_STORAGE,
	JAIL_MASTER_PRIVPORT,
	JAIL_MASTER_KILL,
#define JAIL_SUBPROC (JAIL_MASTER_KILL + 1)
};

enum jail_subproc_e {
	JAIL_SUBPROC_VCC = JAIL_SUBPROC,
	JAIL_SUBPROC_CC,
	JAIL_SUBPROC_VCLLOAD,
	JAIL_SUBPROC_WORKER,
#define JAIL_LIMIT (JAIL_SUBPROC_WORKER + 1)
};

#define ASSERT_JAIL_MASTER(x) assert((x) < JAIL_SUBPROC)

#define ASSERT_JAIL_SUBPROC(x) do {		\
		assert((x) >= JAIL_SUBPROC);	\
		assert((x) < JAIL_LIMIT);	\
	} while (0)

enum jail_fixfd_e {
	JAIL_FIXFD_FILE,
	JAIL_FIXFD_VSMMGT,
	JAIL_FIXFD_VSMWRK,
};

typedef int jail_init_f(char **);
typedef void jail_master_f(enum jail_master_e);
typedef void jail_subproc_f(enum jail_subproc_e);
typedef int jail_make_dir_f(const char *, const char *, struct vsb *);
typedef void jail_fixfd_f(int, enum jail_fixfd_e);

struct jail_tech {
	unsigned		magic;
#define JAIL_TECH_MAGIC		0x4d00fa4d
	const char		*name;
	jail_init_f		*init;
	jail_master_f		*master;
	jail_subproc_f		*subproc;
	jail_make_dir_f		*make_workdir;
	jail_make_dir_f		*make_subdir;
	jail_fixfd_f		*fixfd;
};

void VJ_Init(const char *);
void VJ_master(enum jail_master_e);
void VJ_subproc(enum jail_subproc_e);
int VJ_make_workdir(const char *);
int VJ_make_subdir(const char *, const char *, struct vsb *);
void VJ_fix_fd(int, enum jail_fixfd_e);
void VJ_unlink(const char *);
void VJ_rmdir(const char *);

extern const struct jail_tech jail_tech_unix;
extern const struct jail_tech jail_tech_solaris;
extern const struct jail_tech jail_tech_linux;

/* mgt_main.c */
extern struct vsb *vident;
extern struct VSC_mgt	*VSC_C_mgt;
struct choice {
	const char      *name;
	const void	*ptr;
};

extern const char C_ERR[];	// Things are not as they should be
extern const char C_INFO[];	// Normal stuff, keep a record for later
extern const char C_DEBUG[];	// More detail than you'd normally want
extern const char C_SECURITY[];	// Security issues
extern const char C_CLI[];	// CLI traffic between master and child

/* mgt_param.c */
void MCF_InitParams(struct cli *);
enum mcf_which_e {
	MCF_DEFAULT = 32,
	MCF_MINIMUM = 33,
	MCF_MAXIMUM = 34,
};
void MCF_ParamConf(enum mcf_which_e, const char *param, const char *, ...)
    v_printflike_(3, 4);

void MCF_ParamSet(struct cli *, const char *param, const char *val);
void MCF_ParamProtect(struct cli *, const char *arg);
void MCF_DumpRstParam(void);
void MCF_AddParams(struct parspec *ps);
extern struct params mgt_param;

/* mgt_shmem.c */
void mgt_SHM_Init(void);
void mgt_SHM_static_alloc(const void *, ssize_t size,
    const char *class, const char *ident);
void mgt_SHM_Create(void);
void mgt_SHM_Destroy(int keep);

void mgt_SHM_ChildNew(void);
void mgt_SHM_ChildDestroy(void);

/* mgt_param_tcp.c */
void MCF_TcpParams(void);

/* mgt_util.c */
char *mgt_HostName(void);
void mgt_ProcTitle(const char *comp);
void mgt_DumpRstVsl(void);
struct vsb *mgt_BuildVident(void);
void MGT_Complain(const char *, const char *, ...) v_printflike_(2, 3);
const void *MGT_Pick(const struct choice *, const char *, const char *);
char **MGT_NamedArg(const char *, const char **, const char *);


/* stevedore_mgt.c */
void STV_Config(const char *spec);
void STV_Config_Transient(void);

/* mgt_vcc.c */
void mgt_DumpBuiltin(void);
char *mgt_VccCompile(struct cli *, struct vclprog *, const char *vclname,
    const char *vclsrc, const char *vclsrcfile, int C_flag);

void mgt_vcl_init(void);
void mgt_vcl_startup(struct cli *, const char *vclsrc, const char *origin,
    const char *vclname, int Cflag);
int mgt_push_vcls(struct cli *, unsigned *status, char **p);
int mgt_has_vcl(void);
extern char *mgt_cc_cmd;
extern const char *mgt_vcl_path;
extern const char *mgt_vmod_path;
#define MGT_VCC(t, n, cc) extern t mgt_vcc_ ## n;
#include <tbl/mgt_vcc.h>

#if defined(PTHREAD_CANCELED) || defined(PTHREAD_MUTEX_DEFAULT)
#error "Keep pthreads out of in manager process"
#endif

#define MGT_FEATURE(x)	COM_FEATURE(mgt_param.feature_bits, x)
#define MGT_DO_DEBUG(x)	COM_DO_DEBUG(mgt_param.debug_bits, x)
