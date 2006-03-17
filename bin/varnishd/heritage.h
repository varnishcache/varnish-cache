/*
 * $Id$
 *
 * This file contains the heritage passed when mgt forks cache
 */

struct heritage {
	int	fds[4];
};

extern struct heritage heritage;

void child_main(void);
