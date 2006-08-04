/*
 * $Id$
 */

#include "common.h"

/* mgt_child.c */
void mgt_run(int dflag);
void mgt_start_child(void);
void mgt_stop_child(void);

/* mgt_cli.c */

void mgt_cli_init(void);
void mgt_cli_setup(int fdi, int fdo, int verbose);
void mgt_cli_start_child(int fdi, int fdo);
void mgt_cli_stop_child(void);
int mgt_cli_askchild(int *status, char **resp, const char *fmt, ...);


/* tcp.c */
int open_tcp(const char *port);

#include "stevedore.h"

extern struct stevedore sma_stevedore;
extern struct stevedore smf_stevedore;

#include "hash_slinger.h"

extern struct hash_slinger hsl_slinger;
extern struct hash_slinger hcl_slinger;

