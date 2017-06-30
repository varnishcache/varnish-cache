/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Log tailer for Varnish
 */

#include "config.h"

#include <sys/types.h>
#include <sys/signal.h>
#include <sys/uio.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <signal.h>

#include "vapi/vsm.h"
#include "vapi/vsl.h"
#include "vapi/voptget.h"
#include "vas.h"
#include "vdef.h"
#include "vpf.h"
#include "vsb.h"
#include "vut.h"
#include "miniobj.h"
#include "vqueue.h"
#include "vsa.h"
#include "vss.h"
#include "vtcp.h"

#include "rsm.h"
#include "stat_cnt.h"

#include "cache/cache.h"
#include "hash/hash_slinger.h"

#define freez(x) do { if (x) free(x); x = NULL; } while (0);

static const char progname[] = "varnishreplay";

struct log {
	/* Options */
	const char	*a_arg;
	int		D_opt; // !!!
	const char	*f_arg;
	int		m_arg;
	const char	*t_arg; // !!!
	int		w_arg;
	int		y_opt;
	int		z_opt;

	/* State */
	FILE		*fo;
} LOG;


//static struct vss_addr *addr_info;
static struct suckaddr *addr_info;
static int debug;
static int delay;
static char token[1024];
static char tokname[256];
static int DELAY_DEF = 1;
const int MAX_NTHREAD_DEF = 8192;
static int max_nthread = 8192;

struct token_s {
	VTAILQ_ENTRY(token_s)	list;
	char			*val;
	char			*nval;
};

static int ntoks = 0;
static VTAILQ_HEAD(,token_s) new_token_list = VTAILQ_HEAD_INITIALIZER(new_token_list);
static VTAILQ_HEAD(,token_s) token_list = VTAILQ_HEAD_INITIALIZER(token_list);

static pthread_mutex_t		token_mtx;

static int use_y_header = 1;
static char* y_header = "Y-Varnish:";

// hash //
static const struct hash_slinger *fd_hash;
extern const struct hash_slinger hcl_slinger;

extern struct objhead * create_objhead();


void
HSHR_Init(const struct hash_slinger *slinger)
{

	fd_hash = slinger;
	if (fd_hash->start != NULL)
		fd_hash->start();
}

int
HSHR_Lookup(int fd, int* pval, struct objhead **or)
{
	struct objhead *oh1;
	struct objhead *oh = NULL;
	char buf[DIGEST_LEN];

	memset((void*) &buf, 0, sizeof(buf));
	sprintf(buf, "%u", fd);

	if (pval) {
		oh = create_objhead();
		strcpy((char*) &oh->digest, buf);
		oh->refcnt = *pval;
	}

	oh1 = fd_hash->lookup(NULL, buf, oh ? &oh : NULL);
	if (oh1) {
		if (or) *or = oh1;
		return oh1->refcnt;
	}

	return -1;
}

void
HSHR_Deref(int fd)
{
	struct objhead *oh = NULL;
	int r;

	HSHR_Lookup(fd, NULL, &oh);
	if (oh) r = fd_hash->deref(oh);
}

// hash //

static void
init_mtx(void)
{
	AZ(pthread_mutex_init(&token_mtx, NULL));
}

static int
isprefix(const char *str, const char *prefix, const char **next)
{
	while (*str && *prefix &&
	    tolower((int)*str) == tolower((int)*prefix))
		++str, ++prefix;
	if (*str && *str != ' ')
		return (0);
	if (next) {
		while (*str && *str == ' ')
			++str;
		*next = str;
	}
	return (1);
}

static int
isprefix_ex(const char *str, const char *prefix, const char **next)
{
	int plen = strlen(prefix);
	int cnt = 0;

	while (*str && *prefix &&
	    tolower((int)*str) == tolower((int)*prefix))
		++str, ++prefix, ++cnt;
	if (cnt != plen)
		return (0);
	if (next) {
		while (*str && *str == ' ')
			++str;
		*next = str;
	}
	return (1);
}

#if 0
static int
isequal(const char *str, const char *reference, const char *end)
{

	while (str < end && *str && *reference &&
	    tolower((int)*str) == tolower((int)*reference))
		++str, ++reference;
	if (str != end || *reference)
		return (0);
	return (1);
}
#endif

/*
 * mailbox toolkit
 */

struct message {
	enum VSL_tag_e tag;
	size_t len;
	char *ptr;
	VSTAILQ_ENTRY(message) list;
};

#define MAX_MAILBOX_SIZE 30

struct mailbox {
	pthread_mutex_t lock;
	pthread_cond_t has_mail;
	int open;
	VSTAILQ_HEAD(msgq_head, message) messages;
};

static void
mailbox_create(struct mailbox *mbox)
{
	VSTAILQ_INIT(&mbox->messages);
	pthread_mutex_init(&mbox->lock, NULL);
	pthread_cond_init(&mbox->has_mail, NULL);
	mbox->open = 1;
}

static void
mailbox_destroy(struct mailbox *mbox)
{
	struct message *msg;

	while ((msg = VSTAILQ_FIRST(&mbox->messages))) {
		VSTAILQ_REMOVE_HEAD(&mbox->messages, list);
		free(msg);
	}
	pthread_cond_destroy(&mbox->has_mail);
	pthread_mutex_destroy(&mbox->lock);
}

static void
mailbox_put(struct mailbox *mbox, struct message *msg)
{
	pthread_mutex_lock(&mbox->lock);
	VSTAILQ_INSERT_TAIL(&mbox->messages, msg, list);
	pthread_cond_signal(&mbox->has_mail);
	pthread_mutex_unlock(&mbox->lock);
}

static struct message *
mailbox_get(struct mailbox *mbox)
{
	struct message *msg;

	pthread_mutex_lock(&mbox->lock);
	while ((msg = VSTAILQ_FIRST(&mbox->messages)) == NULL && mbox->open)
		pthread_cond_wait(&mbox->has_mail, &mbox->lock);
	if (msg != NULL)
		VSTAILQ_REMOVE_HEAD(&mbox->messages, list);
	pthread_mutex_unlock(&mbox->lock);
	return (msg);
}

static void
mailbox_close(struct mailbox *mbox)
{
	pthread_mutex_lock(&mbox->lock);
	mbox->open = 0;
	pthread_cond_signal(&mbox->has_mail);
	pthread_mutex_unlock(&mbox->lock);
}

/*
 * thread toolkit
 */

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static void
thread_log(int lvl, int errcode, const char *fmt, ...)
{
	va_list ap;

	if (lvl > debug)
		return;
	pthread_mutex_lock(&log_mutex);
	fprintf(stderr, "%p ", (void *)pthread_self());
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	if (errcode)
		fprintf(stderr, ": %s", strerror(errcode));
	fprintf(stderr, "\n");
	pthread_mutex_unlock(&log_mutex);
}

struct thread_ind_s {
	VSTAILQ_ENTRY(thread_ind_s) list;
	int ind;
};

struct replay_thread {
	pthread_t thread_id;
	struct mailbox mbox;

	int sock;

	int fd;			/* original fd from logs */

	char *method;		/* Request method*/
	char *proto;		/* Protocol version */
	char *url;		/* URL and query string */
	const char *conn;	/* Connection info (keep-alive, close) */
	char *hdr[64];		/* Headers */
	int nhdr;		/* Number of headers */
	int bogus;		/* bogus request */

	char arena[4096];
	int top;
	char line[2048];
	char temp[2048];
	int len;		/* expected response len */
	int status;		/* expected response status */
	int reqcnt;		/* sent req cnt */
	int linecnt;	/* log lines cnt */
	int intstatus;	/* expected internal status */
	struct thread_ind_s *pind; 	/* thread pindex - new alg */
	unsigned yid;
	char tokhdr[2048];	/* header for token */
};

static VSTAILQ_HEAD(,thread_ind_s) thread_free_ind_list = VSTAILQ_HEAD_INITIALIZER(thread_free_ind_list);

static struct replay_thread **threads;
static size_t nthreads;

static int *fd_map;
static int new_alg = 0;

static void print_thread_stat()
{
	int cnt = 0;

	int fd = 0;
	printf("\n Threads:\n");
	for (fd = 0; fd < nthreads; ++fd) {
		if (threads[fd] != NULL) {
			printf("\t %6d: %6d / %6d\n", fd, threads[fd]->reqcnt, threads[fd]->linecnt);
			cnt++;
		}
	}
	printf(" NThreads/slots: %6d / %6d\n", cnt, (int) nthreads);
}

/*
 * Clear thread state
 */
static void
thread_clear(struct replay_thread *thr)
{
	thr->method = thr->proto = thr->url = NULL;
	thr->conn = NULL;
	memset(&thr->hdr, 0, sizeof thr->hdr);
	thr->nhdr = 0;
	thr->bogus = 0;
	memset(&thr->arena, 0, sizeof thr->arena);
	thr->top = 0;
	memset(&thr->line, 0, sizeof thr->line);
	memset(&thr->temp, 0, sizeof thr->temp);
	if (thr->sock != -1)
		close(thr->sock);
	thr->sock = -1;
	thr->len = -1;
	thr->status = -1;
	thr->reqcnt = 0;
	thr->linecnt = 0;
	thr->intstatus = 0;
	thr->yid = 0;
	memset(&thr->tokhdr, 0, sizeof thr->tokhdr);
}

#define THREAD_FAIL ((struct replay_thread *)-1)

static pthread_attr_t thread_attr;


static struct replay_thread *
thread_get(int fd, void *(*thread_main)(void *), bool newsess)
{
	int thr_ind = fd;
	struct thread_ind_s *pind = NULL;
	static int nfds = 0;

	assert(fd != 0);
	if (fd >= nthreads) {
		struct replay_thread **newthreads = threads;
		size_t newnthreads = nthreads;
		size_t newnfds = 0;

		while (fd >= newnthreads)
			newnthreads += newnthreads + 1;

		if (new_alg) {
			newnfds = newnthreads;
			if (newnthreads >= max_nthread) newnthreads = max_nthread;
		}
		if (newnthreads > nthreads || !new_alg) { // ever reallocate with old algorithm
			newthreads = realloc(newthreads,
				newnthreads * sizeof *newthreads);
			XXXAN(newthreads != NULL);
			memset(newthreads + nthreads, 0,
				(newnthreads - nthreads) * sizeof *newthreads);
			threads = newthreads;
		}

		if (new_alg) {
			int i;
			int *new_fd_map = fd_map;

			if (newnfds > nfds && new_alg < 2) {
				new_fd_map = realloc(new_fd_map, newnfds * sizeof *new_fd_map);
				XXXAN(new_fd_map != NULL);
				fd_map = new_fd_map;
				for (i = nfds; i < newnfds; i++) {
					fd_map[i] = -1;
				}
				nfds = newnfds;
			}
			if (newnthreads > nthreads) {
				for (i = nthreads; i < newnthreads; i++) {
					pind = malloc(sizeof (struct thread_ind_s));
					pind->ind = i;
					VSTAILQ_INSERT_TAIL(&thread_free_ind_list, pind, list);
				}
			}
		}

		nthreads = newnthreads;
		set_nthr_rsm(nthreads);
	}

	if (new_alg) {
		if (new_alg < 2) {
			thr_ind = fd_map[fd];
		} else {
			thr_ind = HSHR_Lookup(fd, NULL, NULL);
		}

		if (newsess) {
			// switch to another thr_ind
			if (thr_ind != -1 && threads[thr_ind]) {
				pind = threads[thr_ind]->pind;
//				VSTAILQ_INSERT_TAIL(&thread_free_ind_list, pind, list);
			}

			if (new_alg < 2) {
				thr_ind = fd_map[fd] = -1;
			} else {
				HSHR_Deref(fd);
				thr_ind = -1;
			}
		}
		if (thr_ind == -1) {
			if (VSTAILQ_EMPTY(&thread_free_ind_list)) {
				// should not happened
				thread_log(0, 0, "THREAD_LIST: is empty - nthr:%d", nthreads);
				return (NULL);
			}
			pind = VSTAILQ_FIRST(&thread_free_ind_list);
			thr_ind = pind->ind;
			VSTAILQ_REMOVE_HEAD(&thread_free_ind_list, list);
			if (new_alg < 2) {
				fd_map[fd] = thr_ind;
			} else {
				HSHR_Lookup(fd, &thr_ind, NULL);
			}
			VSTAILQ_INSERT_TAIL(&thread_free_ind_list, pind, list);

			if (threads[thr_ind] != NULL) {
				threads[thr_ind]->fd = fd;
				set_thr_fd_rsm(thr_ind, fd);
			}
		}
	}

	if (threads[thr_ind] == NULL) {
		threads[thr_ind] = calloc(sizeof *threads[thr_ind], 1);
		assert(threads[thr_ind] != NULL);
		threads[thr_ind]->sock = -1;
		thread_clear(threads[thr_ind]);
		mailbox_create(&threads[thr_ind]->mbox);
		if (new_alg) threads[thr_ind]->pind = pind;
		if (pthread_create(&threads[thr_ind]->thread_id, &thread_attr,
		    thread_main, threads[thr_ind]) != 0) {
			thread_log(0, errno, "pthread_create()");
			mailbox_destroy(&threads[thr_ind]->mbox);
			freez(threads[thr_ind]);
			threads[thr_ind] = THREAD_FAIL;
		} else {
			set_thr_fd_rsm(thr_ind, fd);
			threads[thr_ind]->fd = fd;
			thread_log(0, 0, "thread %p:%d started",
			    (void *)threads[thr_ind]->thread_id, fd);
		}
	}
	if (threads[thr_ind] == THREAD_FAIL)
		return (NULL);

	return (threads[thr_ind]);
}

static void
thread_close(int fd)
{
	if (fd == -1) {
		for (fd = 0; fd < nthreads; ++fd)
			thread_close(fd);
		return;
	}

	assert(fd < nthreads);

	if (threads[fd] == NULL)
		return;
	mailbox_close(&threads[fd]->mbox);
	pthread_join(threads[fd]->thread_id, NULL);
	thread_log(0, 0, "thread %p stopped",
	    (void *)threads[fd]->thread_id);
	thread_clear(threads[fd]);
	mailbox_destroy(&threads[fd]->mbox);
	freez(threads[fd]);
}

/*
 * Allocate from thread arena
 */
static void *
thread_alloc(struct replay_thread *thr, size_t len)
{
	void *ptr;

	if (sizeof thr->arena - thr->top < len) {
		thread_log(0, 0, "thread_alloc: not enough space %d for len %d - yid %u", (sizeof thr->arena - thr->top), len, thr->yid);
		return (NULL);
	}
	ptr = thr->arena + thr->top;
	thr->top += len;
	return (ptr);
}

/*
 * Returns a copy of the entire string with leading and trailing spaces
 * trimmed.
 */
static char *
trimline(struct replay_thread *thr, const char *str, size_t len)
{
	char *p;

	/* skip leading space */
	while (*str && isspace(*str))
		++str;

	/* trim trailing space */
	while (len && isspace(str[len - 1]))
		--len;

	/* copy and return */
	if ((p = thread_alloc(thr, len + 1)) == NULL)
		return (NULL);
	memcpy(p, str, len);
	p[len] = '\0';
	return (p);
}

static char *
lookuptoken(const char *s)
{
	char* tok = NULL;

	AZ(pthread_mutex_lock(&token_mtx));
	struct token_s *t;
	int tind = 0;
	VTAILQ_FOREACH(t, &token_list, list)
		if (!strcmp(s, t->val))
			break;
		else tind++;
	if (t == NULL) {
		t = calloc(sizeof *t, 1);
		AN(t);
		REPLACE(t->val, s);
		VTAILQ_INSERT_TAIL(&token_list, t, list);

		struct token_s *nt;
		int sind = 0;
		tind %= ntoks;
		VTAILQ_FOREACH(nt, &new_token_list, list)
			if (sind++ == tind)
				break;
		if (nt != NULL) t->nval = nt->val;
		// else -- error
	}

	if (t && t->nval) tok = t->nval;
	// else -- error
	AZ(pthread_mutex_unlock(&token_mtx));

	return tok;
}

static int
loadtoken(const char *s, char *tn, size_t tnl, char *t, size_t tl)
{
	char *p = strchr(s, '=');
	if (p) {
		size_t len = p - s;
		if (tnl <= len) return 0;
		strncpy(tn, s, len); // cp 'name'
		len = strlen(s);
		if (tl <= len) return 0;
		strncpy(t, s, len); // cp 'name=token'
		return 1;
	}

	return 0;
}

static char *
gettoken(struct replay_thread *thr, const char *phdr)
{
	int tlen = strlen(token);

	if ((! tlen && ! ntoks) || ! phdr) return NULL; // ignore

	const char *ntok = NULL;
	char *pb = strcasestr(phdr, tokname);
	char *pe = NULL;
	int ind = 0;
	int clen = 0;

	int cplen = 0;
	char ctok[1024]; 
	memset(&ctok, 0, sizeof ctok);

	memset(&thr->tokhdr, 0, sizeof thr->tokhdr);
	// copy part before name=val
	if (pb != NULL ) {
		clen = pb - phdr;
		// possible: ' ..name;', '..name=;', '..name=val;', etc..
		memcpy(&thr->tokhdr[ind], phdr, clen); // cp line before 'name'
		ind += clen;

		pe = strchr(pb, ';');

		if (! tlen) {
			cplen = strlen(pb);
			if (pe != NULL) cplen = pe - pb;
			if (cplen - strlen(tokname) == 1) cplen--; // 'name=' ---> 'name'
			memcpy(&ctok, pb, cplen);
		}
	} else { // pb == NULL
		clen = strlen(phdr);
		memcpy(&thr->tokhdr[ind], phdr, clen);
		ind += clen;

		clen = 2; // ilen + 2; // +2 -- for '; '
		memcpy(&thr->tokhdr[ind], "; ", clen);
		ind += clen;

		if (! tlen) memcpy(&ctok, tokname, strlen(tokname));
	}

	if (tlen) ntok = &token[0];
	else if (strlen(ctok)) {
		ntok = lookuptoken(ctok);
		if (ntok == NULL) return NULL;
		tlen = strlen(ntok);
	}

	// copy name=newval and part after
	if (pb != NULL) {
		memcpy(&thr->tokhdr[ind], ntok, tlen); // cp 'name=token'
		ind += tlen;
		if (pe != NULL) {
			clen = strlen(pe);
			memcpy(&thr->tokhdr[ind], pe, clen); // cp rest of line, if extists
		}
	} else { // pb == NULL
		memcpy(&thr->tokhdr[ind], ntok, tlen);
	}

	return &thr->tokhdr[0];
}

static int
loadtfile(const char *fn)
{
	FILE *f;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	int scnt;
	char str[1024];
	char tname[256];
	char tok[1024];

	struct token_s *t;

	if ((f = fopen(fn, "r")) == NULL) {
		perror(fn);
		exit(1);
	}

	memset(&str, 0, sizeof str);
	memset(&tname, 0, sizeof tname);
	memset(&tok, 0, sizeof tok);

	while ((read = getline(&line, &len, f)) != -1) {
		scnt = sscanf(line, "%s", str);
		if (!loadtoken(str, tname, sizeof(tname), tok, sizeof(tok)) || scnt != 1)
			fprintf(stderr,
				"[ loadline ] Wrong line '%s', ignored..\n", line);
		else {
			int addval = 1;
			if (!strlen(tokname)) {
				size_t len = strlen(tname);
				if (sizeof(tokname) <= len) return 0;
				memcpy(&tokname[0], tname, len); // fill tokname
			} else {
				if (strcmp(&tokname[0], tname)) {
					fprintf(stderr,
						"[ loadline ] Only one token name '%s' is supported -- '%s' ignored..\n", tokname, tname);
					addval = 0;
				}
			}

			if (addval) {
				VTAILQ_FOREACH(t, &new_token_list, list)
					if (!strcmp(tok, t->val))
						break;
				if (t == NULL) {
					t = calloc(sizeof *t, 1);
					AN(t);
					REPLACE(t->val, tok);
					VTAILQ_INSERT_TAIL(&new_token_list, t, list);
					ntoks++;
				}
			}
		}
	}

	if (line) free(line);

	return (ntoks > 0);
}

/* Read a line from the socket and return the number of bytes read.
 * After returning, line will point to the read bytes in memory.
 * A line is terminated by \r\n
 */
static int
read_line(struct replay_thread *thr)
{
	int i, len;

	len = 0;
	while (1) {
		if (len + 2 > sizeof thr->line) {
			thread_log(0, 0, "overflow");
			return (-1);
		}

		i = read(thr->sock, thr->line + len, 1);
		if (i < 0) {
			thread_log(0, errno, "read(%d, %p, %d)",
			    thr->sock, thr->line + len, 1);
			return (-1);
		}
		if (i == 0)
			break;
		len += i;
		if (len >= 2 && thr->line[len - 2] == '\r' &&
		    thr->line[len - 1] == '\n') {
			len -= 2;
			break;
		}
	}
	thr->line[len] = '\0';
	return (len);
}

/* Read a block of data from the socket, and do nothing with it.
 * length says how many bytes to read, and the function returns
 * the number of bytes read.
 */
static int
read_block(struct replay_thread *thr, int len)
{
	int n, r, tot;

	for (tot = 0; tot < len; tot += r) {
		n = len - tot;
		if (n > sizeof thr->temp)
			n = sizeof thr->temp;
		r = read(thr->sock, thr->temp, n);
		if (r < 0) {
			thread_log(0, errno, "read(%d, %p, %d)",
			    thr->sock, thr->temp, n);
			return (-1);
		}
		if (r == 0)
			break;
	}
	return (tot);
}

/* Receive the response after sending a request.
 */
static int
receive_response(struct replay_thread *thr)
{
	const char *next;
	int line_len;
	long chunk_length, content_length;
	int chunked, connclose, failed, xid;
	int n, status, intstatus, encoded;
	int fd = thr->fd;

	content_length = 0;
	chunked = connclose = failed = xid = intstatus = encoded = 0;

	if (new_alg) fd = thr->pind->ind;

	/* Read header */
	for (;;) {
		line_len = read_line(thr);
		if (line_len < 0)
			return (-1);
		thread_log(3, 0, "< %.*s", line_len, thr->line);
		if (line_len == 0)
			break;
		if (strncmp(thr->line, "HTTP", 4) == 0) {
			sscanf(thr->line, "%*s %d %*s\r\n", &status);
			failed = (status != 200);
		} else if (isprefix(thr->line, "content-length:", &next)) {
			content_length = strtol(next, NULL, 10);
		} else if (isprefix(thr->line, "transfer-encoding:", &next)) {
			chunked = (strcasecmp(next, "chunked") == 0);
		} else if (isprefix(thr->line, "connection:", &next)) {
			connclose = (strcasecmp(next, "close") == 0);
		} else if (isprefix(thr->line, "content-encoding:", &next)) {
			encoded = (strcasecmp(next, "gzip") == 0);
		} else if (isprefix(thr->line, "X-Varnish:", &next)) {
			xid = atoi(next);
		} else if (isprefix_ex(thr->line, "Internal-Status:", &next)) {
			intstatus = atoi(next);
		}
	}

	if (status == thr->status) inc_cnt(STT_IND); 
	else {
		thread_log(1, 0, "stat(!exp) - istat(exp): %d(%d) - %d(%d) xid/yid: %u/%u", status, thr->status, intstatus, thr->intstatus, xid, thr->yid);
		inc_cnt(STT_NEQ_IND);
		if (status == 200) inc_cnt(STT_200_NEQ_IND);
	}
	switch (status) {
		case 200:
			inc_cnt(STT_200_IND);
			break;
		default:
			inc_cnt(STT_NEQ_200_IND);
			break;
	}

	inc_stt_resp_rsm(status, status != thr->status, fd);
	thread_log(2, 0, "status: %d", status);

	if (!chunked) {
		if (content_length != thr->len) {
			thread_log(2, 0, "cont_len: %d != expected: %d (chunked: %d encoded: %d)", content_length, thr->len, chunked, encoded);
			thread_log(2, 0, "stat(exp): %d(%d) url: %s", status, thr->status, thr->url);
			if (!encoded) inc_cnt(LEN_NEQ_IND);
			else inc_cnt(ENC_NEQ_IND);
		} else if (content_length == thr->len) {
			if (!encoded) inc_cnt(LEN_IND);
			else {
				inc_cnt(ENC_IND);
				inc_err_rsm(RSM_ENC_IND);
			}
		}
	}

	/* Read body */
	if (chunked) {
		/* Chunked encoding, read size and bytes until no more */
		thread_log(2, 0, "chunked encoding");
		for (;;) {
			line_len = read_line(thr);
			if (line_len < 0)
				return (-1);
			/* read_line() guarantees null-termination */
			chunk_length = strtol(thr->line, NULL, 16);
			if (chunk_length > 0)
				content_length += chunk_length;
			if (chunk_length == 0)
				break;
			if ((n = read_block(thr, chunk_length)) < 0)
				return (-1);
			if (n < chunk_length)
				thread_log(0, 0, "short read: %d/%ld",
				    n, chunk_length);
			thread_log(2, 0, "chunk length: %ld", chunk_length);
			thread_log(2, 0, "bytes read: %d", n);
			/* trailing CR LF */
			if ((n = read_line(thr)) < 0)
				return (-1);
		}

		if (content_length != thr->len) {
			thread_log(2, 0, "cont_len: %d != expected: %d (chunked: %d encoded: %d)", content_length, thr->len, chunked, encoded);
			thread_log(2, 0, "stat(exp): %d(%d) url: %s", status, thr->status, thr->url);
			if (!encoded) inc_cnt(CHK_NEQ_IND);
			else inc_cnt(CHK_ENC_NEQ_IND);
		} else if (content_length == thr->len) {
			if (!encoded) {
				inc_cnt(CHK_IND);
				inc_err_rsm(RSM_CHK_IND);
			} else {
				inc_cnt(CHK_ENC_IND);
				inc_err_rsm(RSM_CHK_ENC_IND);
			}
		}

		/* trailing CR LF */
		n = read_line(thr);
		if (n < 0)
			return (-1);
	} else if (content_length > 0) {
		/* Fixed body size, read content_length bytes */
		thread_log(2, 0, "fixed length");
		thread_log(2, 0, "content length: %ld", content_length);
		if ((n = read_block(thr, content_length)) < 0)
			return (1);
		if (n < content_length)
			thread_log(0, 0, "short read: %d/%ld",
			    n, content_length);
		thread_log(2, 0, "bytes read: %d", n);
	} else {
		/* No body --> stop reading. */
		thread_log(2, 0, "no body");
		return (-1);
	}

	return (connclose);
}

static void *
replay_thread(void *arg)
{
	struct iovec iov[6];
	char space[1] = " ", crlf[2] = "\r\n";
	struct replay_thread *thr = arg;
	struct message *msg;
	enum VSL_tag_e tag;
	char *ptr;
	const char *next;
	size_t len;
	int fd = thr->fd;

	int i;

	int reopen = 1;

	if (new_alg) fd = thr->pind->ind;

	while ((msg = mailbox_get(&thr->mbox)) != NULL) {
		thr->linecnt++;
		inc_thr_line_rsm(fd);
		tag = msg->tag;
		ptr = msg->ptr;
		len = msg->len;

		thread_log(3, 0, "%s(%s):%d fd:%d(%d)", VSL_tags[tag], msg->ptr, msg->len, fd, thr->fd);

		switch (tag) {
		case SLT_ReqMethod:
			if (thr->method != NULL)
				thr->bogus = 1;
			else
				thr->method = trimline(thr, ptr, len);
			break;

		case SLT_ReqURL:
			if (thr->url != NULL)
				thr->bogus = 2;
			else
				thr->url = trimline(thr, ptr, len);
			break;

		case SLT_ReqProtocol:
			if (thr->proto != NULL)
				thr->bogus = 3;
			else
				thr->proto = trimline(thr, ptr, len);
			break;

		case SLT_ReqHeader:
			if (thr->nhdr >= sizeof thr->hdr / sizeof *thr->hdr) {
				thr->bogus = 4;
			} else {
				thr->hdr[thr->nhdr++] = trimline(thr, ptr, len);
				if (isprefix(ptr, "connection:", &next))
					thr->conn = trimline(thr, next, len);
				else if (isprefix(ptr, "Cookie:", &next)) {
					char* thdr = gettoken(thr, thr->hdr[thr->nhdr-1]);
					if (thdr) thr->hdr[thr->nhdr-1] = thdr;
				}
			}
			break;

		case SLT_Length:
			if (thr->len == -1)
				thr->len = atoi(trimline(thr, ptr, len));
			break;

		case SLT_RespStatus:
			thr->status = atoi(trimline(thr, ptr, len));
			break;

		case SLT_RespHeader:
			if (isprefix(ptr, "X-Varnish:", &next)) {
				if (thr->nhdr >= sizeof thr->hdr / sizeof *thr->hdr) {
					thr->bogus = 41;
				} else {
					thr->hdr[thr->nhdr++] = trimline(thr, ptr, len);

					if (use_y_header) {
						size_t y_len = strlen(y_header);
						char* phdr = thr->hdr[thr->nhdr-1];
						memcpy(phdr, y_header, y_len);
						phdr += y_len;
						thr->yid = atoi(phdr);
					}
				}
			}
			break;

		case SLT_VCL_Log:
			if (isprefix_ex(ptr, "Internal-Status:", &next)) {
				if (thr->nhdr >= sizeof thr->hdr / sizeof *thr->hdr) {
					thr->bogus = 41;
				} else {
					thr->intstatus = atoi(trimline(thr, next, len));
				}
			}
			break;

		default:
			break;
		}

		freez(msg->ptr);
		freez(msg);

		if (tag != SLT_End) // SLT_ReqEnd
			continue;

		if (!thr->method || !thr->url || !thr->proto) {
			thr->bogus = 5;
		} else if (strcmp(thr->method, "GET") != 0 &&
		    strcmp(thr->method, "HEAD") != 0) {
			thr->bogus = 6;
			inc_cnt(MTD_NS_IND);
			inc_err_rsm(RSM_MTD_NS_IND);
		} else if (strcmp(thr->proto, "HTTP/1.0") == 0) {
			reopen = !(thr->conn &&
			    strcasecmp(thr->conn, "keep-alive") == 0);
		} else if (strcmp(thr->proto, "HTTP/1.1") == 0) {
			reopen = (thr->conn &&
			    strcasecmp(thr->conn, "close") == 0);
		} else {
			thr->bogus = 7;
		}

		if (thr->bogus) {
			if (thr->bogus != 6) {
				thread_log(1, 0, "bogus %d yid %u method '%s' url '%s' proto '%s'", thr->bogus, thr->yid, thr->method, thr->url, thr->proto);
				inc_cnt(INR_ERR_IND);
				inc_err_rsm(RSM_ERR_IND);
			} else
				thread_log(2, 0, "bogus %d yid %u", thr->bogus, thr->yid);

			goto clear;
		}

		if (thr->sock == -1) {
			for (;;) {
				thread_log(2, 0, "sleeping before connect... - yid %u", thr->yid);
				usleep(1000 * (fd % 3001));
				thr->sock = VTCP_connect(addr_info, 0);
				if (thr->sock >= 0)
					break;
				thread_log(0, errno, "connect failed - yid %u", thr->yid);
			}
		}

		thread_log(2, 0, "%s %s %s %u", thr->method, thr->url, thr->proto, thr->yid);

		iov[0].iov_base = thr->method;
		iov[0].iov_len = strlen(thr->method);
		iov[2].iov_base = thr->url;
		iov[2].iov_len = strlen(thr->url);
		iov[4].iov_base = thr->proto;
		iov[4].iov_len = strlen(thr->proto);
		iov[1].iov_base = iov[3].iov_base = space;
		iov[1].iov_len = iov[3].iov_len = 1;
		iov[5].iov_base = crlf;
		iov[5].iov_len = 2;
		if (writev(thr->sock, iov, 6) == -1) {
			thread_log(0, errno, "writev() - yid:%u", thr->yid);
			goto close;
		}

		for (i = 0; i < thr->nhdr; ++i) {
			thread_log(3, 0, "%d %s", i, thr->hdr[i]);
			iov[0].iov_base = thr->hdr[i];
			iov[0].iov_len = strlen(thr->hdr[i]);
			iov[1].iov_base = crlf;
			iov[1].iov_len = 2;
			if (writev(thr->sock, iov, 2) == -1) {
				thread_log(0, errno, "writev() - yid:%u", thr->yid);
				goto close;
			}
		}
		if (write(thr->sock, crlf, 2) == -1) {
			thread_log(0, errno, "writev() - yid:%u", thr->yid);
			goto close;
		} else {
			thr->reqcnt++;
			inc_thr_req_rsm(fd);
		}
		if (receive_response(thr) || reopen) {
close:
			thread_log(2, 0, "close - yid:%u", thr->yid);
			assert(thr->sock != -1);
			close(thr->sock);
			thr->sock = -1;
		}

clear:
		/* clean up */
		thread_clear(thr);

		sleep(delay);
	}

	/* leftovers */
	thread_clear(thr);

	return (0);
}

static int
gen_traffic(struct VSL_data *vsl, struct VSL_transaction * const pt[],
    void *priv)
{
	struct replay_thread *thr;
	struct message *msg;

	struct VSL_transaction *t;
	unsigned tag;
	unsigned fd;
	unsigned len;
	const char *ptr;

	int skip;

	(void)priv;

	for (t = pt[0]; t != NULL; t = *++pt) {
		skip = 0;
		while (skip == 0 && 1 == VSL_Next(t->c)) {
			tag = VSL_TAG(t->c->rec.ptr);
			fd = VSL_ID(t->c->rec.ptr);
			len = VSL_LEN(t->c->rec.ptr);
			ptr = VSL_CDATA(t->c->rec.ptr);

			if (fd == 0 || !(VSL_CLIENT(t->c->rec.ptr) || tag == SLT_Length)) // expected len from backend resp
				continue;

			thread_log(4, 0, "%d %s", fd, VSL_tags[tag]);
			thr = thread_get(fd, replay_thread, (tag == SLT_ReqStart));
			if (thr == NULL)
				return (0);
			msg = malloc(sizeof (struct message));
			msg->tag = tag;
			msg->len = len;
			msg->ptr = calloc(len+1, 1);
			AN(msg->ptr);
			memcpy(msg->ptr, ptr, len);
			mailbox_put(&thr->mbox, msg);
		}
	}

	return (0);
}

static int __match_proto__(vss_resolved_f)
cb(void *priv, const struct suckaddr *sa)
{
	(void)priv;
	addr_info = VSA_Clone(sa);
	return(0);
}

/* Initiate a connection to <address> by resolving the
 * hostname and returning a struct with necessary
 * connection info.
 */
static int
init_connection(const char *address)
{
	const char *err;
	int error;

	error = VSS_resolver(address, NULL, cb, NULL, &err);
	if (err != NULL) {
		thread_log(0, 0, "Could not connect to server");
		exit(2);
	}
	AZ(error);

	return error;
}

/*--------------------------------------------------------------------*/

static void
sig_handler(int sig)
{
	static int first = 1;
	switch (sig) {
	case SIGHUP:
		prn_cnt(first);
		if (first) first = 0;
		break;
	case SIGUSR1:
		prn_cnt(true);
		exit(0);
		break;
	default:
		break;
	}
}

static void
usage(int status)
{
	fprintf(stderr,
		"usage: %s [-D] -a address:port -r logfile [-t name=token] [-f tokfile] [-yz] [-w delay]\n", progname);
	fprintf(stderr, "\t tokfile format:\n\t\t tok=val1\n\t\t ...\n\t\t tok=valn\n");
	fprintf(stderr, "\t '-y' \t\t use 'Y-Varnish' header [%d]\n", use_y_header);
	fprintf(stderr, "\t '-z' \t\t use new thread choice algorithm [%d]\n", new_alg);
	if (debug)
		fprintf(stderr, "\t '-m <int>' \t\t max thread number [%d]\n", max_nthread);

	exit(status);
}
/*
static void
openout(int append)
{

	AN(LOG.w_arg);
	if (LOG.A_opt)
		LOG.fo = fopen(LOG.w_arg, append ? "a" : "w");
	else
		LOG.fo = VSL_WriteOpen(VUT.vsl, LOG.w_arg, append, 0);
	if (LOG.fo == NULL)
		VUT_Error(2, "Can't open output file (%s)",
		    LOG.A_opt ? strerror(errno) : VSL_Error(VUT.vsl));
	VUT.dispatch_priv = LOG.fo;
}

static int __match_proto__(VUT_cb_f)
rotateout(void)
{

	AN(LOG.w_arg);
	AN(LOG.fo);
	fclose(LOG.fo);
	openout(1);
	AN(LOG.fo);
	return (0);
}

static int __match_proto__(VUT_cb_f)
flushout(void)
{

	AN(LOG.fo);
	if (fflush(LOG.fo))
		return (-5);
	return (0);
}

static int __match_proto__(VUT_cb_f)
sighup(void)
{
	return (1);
}
*/

int
main(int argc, char * const *argv)
{
	int opt;

	memset(&LOG, 0, sizeof LOG);
	VUT_Init(progname);

	const char *address = NULL;
	int t_flag = 0, f_flag = 0;

	debug = 0;
	delay = DELAY_DEF;

	while ((opt = getopt(argc, argv, vopt_optstring)) != -1) {
		switch (opt) {
		case 'a':
			/* address */
			address = optarg;
			LOG.a_arg = address;
			break;
/*
		case 'A':
			/ * Text output * /
			LOG.A_opt = 1;
			break;
*/
		case 'D':
			++debug;
			LOG.D_opt = debug;
			break;
		case 'h':
			/* Usage help */
			usage(0);
			break;
		case 't':
			LOG.t_arg = optarg;
			if (! loadtoken(optarg, tokname, sizeof(tokname), token, sizeof(token))) usage(1);
t_flag = 1;
			break;
		case 'f':
			LOG.f_arg = optarg;
			if (! loadtfile(optarg)) usage(1);
f_flag = 1;
			break;
		case 'm':
			max_nthread = atoi(optarg);
			if (max_nthread < 0) max_nthread = MAX_NTHREAD_DEF;
			LOG.m_arg = max_nthread;
			break;
		case 'w':
//			/* Write to file */
//			REPLACE(LOG.w_arg, optarg);
			/* delay */
			delay = atoi(optarg);
			LOG.w_arg = delay;
			if (delay < 0) delay = DELAY_DEF;
			break;
		case 'y':
			LOG.y_opt = 0;
			use_y_header = 0;
			break;
		case 'z':
			new_alg++;
			LOG.z_opt = new_alg;
			break;
		default:
			if (!VUT_Arg(opt, optarg))
				usage(1);
		}
	}

	if (optind != argc)
		usage(1);

/*
	if (VUT.D_opt && !LOG.w_arg)
		VUT_Error(1, "Missing -w option");

	/ * Setup output * /
	if (LOG.A_opt || !LOG.w_arg)
		VUT.dispatch_f = VSL_PrintTransactions;
	else
		VUT.dispatch_f = VSL_WriteTransactions;
	VUT.sighup_f = sighup;
	if (LOG.w_arg) {
		openout(LOG.a_opt);
		AN(LOG.fo);
		if (VUT.D_opt)
			VUT.sighup_f = rotateout;
	} else
		LOG.fo = stdout;
	VUT.idle_f = flushout;
*/

	if (address == NULL) {
		usage(1);
	}

	if (t_flag && f_flag)
		usage(1);

	init_connection(address);

	init_mtx();

	signal(SIGPIPE, SIG_IGN);

	pthread_attr_init(&thread_attr);

	rsm_init(argv[0]);
	cnt_init();
	signal(SIGHUP, sig_handler);
	signal(SIGUSR1, sig_handler);

	/*
	 * XXX: seting the stack size manually reduces the memory usage
	 * XXX: (allowing more threads) and increases speed (?)
	 */
	pthread_attr_setstacksize(&thread_attr, 32768);

	if (new_alg)
		VSTAILQ_INIT(&thread_free_ind_list);

	LOG.fo = stdout;
	VUT.dispatch_f = gen_traffic;

	VUT_Setup();

	HSHR_Init(&hcl_slinger);

	VUT_Main();

	VUT_Fini();

//	(void)flushout();

	thread_close(-1);

	print_thread_stat();
	prn_cnt(true);

	exit(0);
}
