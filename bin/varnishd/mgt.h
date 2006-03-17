/*
 * $Id$
 */

extern struct event_base *eb;

void mgt_child_start(void);
void mgt_child_stop(void);
void mgt_sigchld(int, short, void *);

typedef void mgt_ccb_f(unsigned, const char *, void *);
void mgt_child_request(mgt_ccb_f *, void *, char **argv, const char *fmt, ...);
