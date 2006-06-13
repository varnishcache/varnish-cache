/*
 * $Id$
 */

extern struct event_base *mgt_eb;

void mgt_child_start(void);
void mgt_child_stop(void);
void mgt_sigchld(int, short, void *);

typedef void mgt_ccb_f(unsigned, const char *, void *);
void mgt_child_request(mgt_ccb_f *, void *, char **argv, const char *fmt, ...);

/* tcp.c */
int open_tcp(const char *port);

#include "_stevedore.h"

extern struct stevedore sma_stevedore;
