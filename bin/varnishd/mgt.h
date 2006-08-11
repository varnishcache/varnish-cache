/*
 * $Id$
 */

#include "common.h"
#include "miniobj.h"

#include "libvarnish.h"

extern struct evbase	*mgt_evb;

/* mgt_child.c */
void mgt_run(int dflag, const char *Tflag);
extern pid_t mgt_pid, child_pid;

/* mgt_cli.c */

void mgt_cli_init(void);
void mgt_cli_setup(int fdi, int fdo, int verbose);
int mgt_cli_askchild(unsigned *status, char **resp, const char *fmt, ...);
void mgt_cli_start_child(int fdi, int fdo);
void mgt_cli_stop_child(void);
int mgt_cli_telnet(const char *port);

/* mgt_vcc.c */
void mgt_vcc_init(void);
int mgt_vcc_default(const char *bflag, const char *fflag);
int mgt_push_vcls_and_start(unsigned *status, char **p);

/* tcp.c */
int open_tcp(const char *port, int http);

#include "stevedore.h"

extern struct stevedore sma_stevedore;
extern struct stevedore smf_stevedore;

#include "hash_slinger.h"

extern struct hash_slinger hsl_slinger;
extern struct hash_slinger hcl_slinger;

