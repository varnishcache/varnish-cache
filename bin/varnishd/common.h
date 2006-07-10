/*
 * $Id$
 */

struct sockaddr;

/* shmlog.c */
void VSL_MgtInit(const char *fn, unsigned size);
extern struct varnish_stats *VSL_stats;

/* tcp.c */
#define TCP_ADDRBUFFSIZE	64	/* Sizeof ascii representation */

void TCP_name(struct sockaddr *addr, unsigned l, char *buf);
void TCP_myname(int sock, char *buf);


