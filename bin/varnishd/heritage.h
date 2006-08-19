/*
 * $Id$
 *
 * This file contains the heritage passed when mgt forks cache
 */

struct heritage {

	/*
	 * Two pipe(2)'s for CLI connection between cache and mgt.
	 * cache reads [2] and writes [1].  Mgt reads [0] and writes [3].
	 */
	int	fds[4];

	/* Socket from which to accept connections */
	int			socket;

	/* Share memory log fd and size (incl header) */
	int			vsl_fd;
	unsigned		vsl_size;

	/* Storage method */
	struct stevedore	*stevedore;

	/* Hash method */
	struct hash_slinger	*hash;

};

struct params {

	/* TTL used for lack of anything better */
	unsigned		default_ttl;

	/* Worker threads */
	unsigned		wthread_min;
	unsigned		wthread_max;
	unsigned		wthread_timeout;

	/* Memory allocation hints */
	unsigned		mem_workspace;

	/* Acceptor hints */
	unsigned		sess_timeout;
	unsigned		send_timeout;
};

extern struct params *params;
extern struct heritage heritage;

void child_main(void);
