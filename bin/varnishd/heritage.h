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

	/*
	 * Two sockets from which to accept connections, one bound to
	 * loopback only and one bound for wildcard (or possibly a specific
	 * interface IP number).
	 */
#define HERITAGE_NSOCKS		2	/* IPv4 + IPv6 */
	int			sock_local[HERITAGE_NSOCKS];
	int			sock_remote[HERITAGE_NSOCKS];

	/* Share memory log fd and size (incl header) */
	int			vsl_fd;
	unsigned		vsl_size;

	/* Initial VCL file */
	char			*vcl_file;

	/* Storage method */
	struct stevedore	*stevedore;

	/* Hash method */
	struct hash_slinger	*hash;

	unsigned		default_ttl;

	/* Worker threads */
	unsigned		wthread_min, wthread_max;
};

extern struct heritage heritage;

void child_main(void);
