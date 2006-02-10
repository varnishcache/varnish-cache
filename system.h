/*
 * $Id$
 */

#ifndef SYSTEM_H_INCLUDED
#define SYSTEM_H_INCLUDED

typedef struct system system_t;

struct system {
	int ncpu;
	pid_t pid;
};

extern system_t sys;

void system_init_ncpu(void);
void system_init(void);
pid_t system_fork(void);

#endif
