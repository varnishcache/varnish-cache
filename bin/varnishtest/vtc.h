/*-
 * Copyright (c) 2008-2011 Varnish Software AS
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

#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif

#include "vdef.h"

#include "miniobj.h"
#include "vas.h"
#include "vqueue.h"
#include "vsb.h"

#define VTC_CHECK_NAME(vl, nm, type, chr)				\
	do {								\
		AN(nm);							\
		if (*(nm) != chr)					\
			vtc_fatal(vl,					\
			    type " name must start with '%c' (got %s)",	\
			    chr, nm);					\
	} while (0)

struct vtclog;
struct suckaddr;

#define CMD_ARGS char * const *av, void *priv, struct vtclog *vl

typedef void cmd_f(CMD_ARGS);

struct cmds {
	const char	*name;
	cmd_f		*cmd;
};

void parse_string(struct vtclog *vl, void *priv, const char *spec);
int fail_out(void);

#define CMD_GLOBAL(n) cmd_f cmd_##n;
#define CMD_TOP(n) cmd_f cmd_##n;
#include "cmds.h"

extern volatile sig_atomic_t vtc_error; /* Error, bail out */
extern int vtc_stop;		/* Abandon current test, no error */
extern pthread_t	vtc_thread;
extern int iflg;
extern vtim_dur vtc_maxdur;
extern char *vmod_path;
extern struct vsb *params_vsb;
extern int leave_temp;
extern int ign_unknown_macro;
extern const char *default_listen_addr;

void init_server(void);
void init_syslog(void);
void init_tunnel(void);

/* Sessions */
struct vtc_sess *Sess_New(struct vtclog *vl, const char *name);
void Sess_Destroy(struct vtc_sess **spp);
int Sess_GetOpt(struct vtc_sess *, char * const **);
int sess_process(struct vtclog *vl, struct vtc_sess *,
    const char *spec, int sock, int *sfd, const char *addr);

typedef int sess_conn_f(void *priv, struct vtclog *);
typedef void sess_disc_f(void *priv, struct vtclog *, int *fd);
pthread_t
Sess_Start_Thread(
    void *priv,
    struct vtc_sess *vsp,
    sess_conn_f *conn,
    sess_disc_f *disc,
    const char *listen_addr,
    int *asocket,
    const char *spec
);

char * synth_body(const char *len, int rnd);

void cmd_server_gen_vcl(struct vsb *vsb);
void cmd_server_gen_haproxy_conf(struct vsb *vsb);

void vtc_log_set_cmd(struct vtclog *vl, const struct cmds *cmds);
void vtc_loginit(char *buf, unsigned buflen);
struct vtclog *vtc_logopen(const char *id, ...) v_printflike_(1, 2);
void vtc_logclose(void *arg);
void vtc_log(struct vtclog *vl, int lvl, const char *fmt, ...)
    v_printflike_(3, 4);
void vtc_fatal(struct vtclog *vl, const char *, ...)
    v_noreturn_ v_printflike_(2,3);
void vtc_dump(struct vtclog *vl, int lvl, const char *pfx,
    const char *str, int len);
void vtc_hexdump(struct vtclog *, int , const char *, const void *, unsigned);

int vtc_send_proxy(int fd, int version, const struct suckaddr *sac,
    const struct suckaddr *sas);

int exec_file(const char *fn, const char *script, const char *tmpdir,
    char *logbuf, unsigned loglen);

void macro_undef(struct vtclog *vl, const char *instance, const char *name);
void macro_def(struct vtclog *vl, const char *instance, const char *name,
    const char *fmt, ...) v_printflike_(4, 5);
unsigned macro_isdef(const char *instance, const char *name);
void macro_cat(struct vtclog *, struct vsb *, const char *, const char *);
struct vsb *macro_expand(struct vtclog *vl, const char *text);
struct vsb *macro_expandf(struct vtclog *vl, const char *, ...)
    v_printflike_(2, 3);

typedef char* macro_f(int, char *const *, const char **);
void extmacro_def(const char *name, macro_f *func, const char *fmt, ...)
    v_printflike_(3, 4);

struct http;
void cmd_stream(CMD_ARGS);
void start_h2(struct http *hp);
void stop_h2(struct http *hp);
void b64_settings(const struct http *hp, const char *s);

/* vtc_gzip.c */
void vtc_gunzip(struct http *, char *, long *);
void vtc_gzip_cmd(struct http *hp, char * const *argv, char **body, long *bodylen);

/* vtc_subr.c */
struct vsb *vtc_hex_to_bin(struct vtclog *vl, const char *arg);
void vtc_expect(struct vtclog *, const char *, const char *, const char *,
    const char *, const char *);
void vtc_wait4(struct vtclog *, long, int, int, int);
void *vtc_record(struct vtclog *, int, struct vsb *);
