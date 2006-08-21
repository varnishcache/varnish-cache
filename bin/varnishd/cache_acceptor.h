/* 
 * $Id$ 
 */

struct sess;

typedef void acceptor_init_f(void);
typedef void acceptor_recycle_f(struct sess *);

struct acceptor {
	const char 		*name;
	acceptor_init_f		*init;
	acceptor_recycle_f	*recycle;
};

#if defined(HAVE_EPOLL_CTL)
extern struct acceptor acceptor_epoll;
#endif

#if defined(HAVE_KQUEUE)
extern struct acceptor acceptor_kqueue;
#endif

#if defined(HAVE_POLL)
extern struct acceptor acceptor_poll;
#endif

/* vca_acceptor.c */
struct sess *vca_accept_sess(int fd);
void vca_handover(struct sess *sp, int bad);
void vca_handfirst(struct sess *sp);

