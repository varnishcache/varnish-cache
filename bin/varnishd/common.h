/*
 * $Id$
 */

struct sockaddr;

/* shmlog.c */
void VSL_MgtInit(const char *fn, unsigned size);
extern struct varnish_stats *VSL_stats;

/* tcp.c */
/* NI_MAXHOST and NI_MAXSERV are ridiculously long for numeric format */
#define TCP_ADDRBUFSIZE		64
#define TCP_PORTBUFSIZE		16

void TCP_name(struct sockaddr *addr, unsigned l, char *abuf, unsigned alen, char *pbuf, unsigned plen);
void TCP_myname(int sock, char *abuf, unsigned alen, char *pbuf, unsigned plen);
int TCP_parse(const char *str, char **addr, char **port);
int TCP_open(const char *addr, const char *port, int http);
