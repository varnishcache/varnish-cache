/*-
 * Copyright (c) 2006 Linpro AS
 * All rights reserved.
 *
 * Author: Cecilie Fritzvold <cecilihf@linpro.no>
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
 * $Id$
 */

#include "config.h"

#include <sys/types.h>
#include <sys/signal.h>
#include <sys/uio.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vqueue.h"

#include "libvarnish.h"
#include "varnishapi.h"
#include "vss.h"

#ifndef HAVE_STRNDUP
#include "compat/strndup.h"
#endif

#define freez(x) do { if (x) free(x); x = NULL; } while (0);

static struct vss_addr *addr_info;
static int debug;

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
	enum shmlogtag tag;
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

struct thread {
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
};

static struct thread **threads;
static size_t nthreads;

/*
 * Clear thread state
 */
static void
thread_clear(struct thread *thr)
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
}

#define THREAD_FAIL ((struct thread *)-1)

static pthread_attr_t thread_attr;

static struct thread *
thread_get(int fd, void *(*thread_main)(void *))
{

	assert(fd != 0);
	if (fd >= nthreads) {
		struct thread **newthreads = threads;
		size_t newnthreads = nthreads;

		while (fd >= newnthreads)
			newnthreads += newnthreads + 1;
		newthreads = realloc(newthreads,
		    newnthreads * sizeof *newthreads);
		XXXAN(newthreads != NULL);
		memset(newthreads + nthreads, 0,
		    (newnthreads - nthreads) * sizeof *newthreads);
		threads = newthreads;
		nthreads = newnthreads;
	}
	if (threads[fd] == NULL) {
		threads[fd] = malloc(sizeof *threads[fd]);
		assert(threads[fd] != NULL);
		threads[fd]->sock = -1;
		thread_clear(threads[fd]);
		mailbox_create(&threads[fd]->mbox);
		if (pthread_create(&threads[fd]->thread_id, &thread_attr,
		    thread_main, threads[fd]) != 0) {
			thread_log(0, errno, "pthread_create()");
			mailbox_destroy(&threads[fd]->mbox);
			freez(threads[fd]);
			threads[fd] = THREAD_FAIL;
		} else {
			threads[fd]->fd = fd;
			thread_log(0, 0, "thread %p:%d started",
			    (void *)threads[fd]->thread_id, fd);
		}
	}
	if (threads[fd] == THREAD_FAIL)
		return (NULL);
	return (threads[fd]);
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
thread_alloc(struct thread *thr, size_t len)
{
	void *ptr;

	if (sizeof thr->arena - thr->top < len)
		return (NULL);
	ptr = thr->arena + thr->top;
	thr->top += len;
	return (ptr);
}

/*
 * Returns a copy of the entire string with leading and trailing spaces
 * trimmed.
 */
static char *
trimline(struct thread *thr, const char *str)
{
	size_t len;
	char *p;

	/* skip leading space */
	while (*str && *str == ' ')
		++str;

	/* seek to end of string */
	for (len = 0; str[len]; ++len)
		 /* nothing */ ;

	/* trim trailing space */
	while (len && str[len - 1] == ' ')
		--len;

	/* copy and return */
	if ((p = thread_alloc(thr, len + 1)) == NULL)
		return (NULL);
	memcpy(p, str, len);
	p[len] = '\0';
	return (p);
}

/* Read a line from the socket and return the number of bytes read.
 * After returning, line will point to the read bytes in memory.
 * A line is terminated by \r\n
 */
static int
read_line(struct thread *thr)
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
read_block(struct thread *thr, int len)
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
receive_response(struct thread *thr)
{
	const char *next;
	int line_len;
	long chunk_length, content_length;
	int chunked, connclose, failed;
	int n, status;

	content_length = 0;
	chunked = connclose = failed = 0;

	/* Read header */
	for (;;) {
		line_len = read_line(thr);
		if (line_len < 0)
			return (-1);
		thread_log(2, 0, "< %.*s", line_len, thr->line);
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
		}
	}

	thread_log(1, 0, "status: %d", status);

	/* Read body */
	if (chunked) {
		/* Chunked encoding, read size and bytes until no more */
		thread_log(1, 0, "chunked encoding");
		for (;;) {
			line_len = read_line(thr);
			if (line_len < 0)
				return (-1);
			/* read_line() guarantees null-termination */
			chunk_length = strtol(thr->line, NULL, 16);
			if (chunk_length == 0)
				break;
			if ((n = read_block(thr, chunk_length)) < 0)
				return (-1);
			if (n < chunk_length)
				thread_log(0, 0, "short read: %d/%ld",
				    n, chunk_length);
			thread_log(1, 0, "chunk length: %ld", chunk_length);
			thread_log(1, 0, "bytes read: %d", n);
			/* trailing CR LF */
			if ((n = read_line(thr)) < 0)
				return (-1);
		}
		/* trailing CR LF */
		n = read_line(thr);
		if (n < 0)
			return (-1);
	} else if (content_length > 0) {
		/* Fixed body size, read content_length bytes */
		thread_log(1, 0, "fixed length");
		thread_log(1, 0, "content length: %ld", content_length);
		if ((n = read_block(thr, content_length)) < 0)
			return (1);
		if (n < content_length)
			thread_log(0, 0, "short read: %d/%ld",
			    n, content_length);
		thread_log(1, 0, "bytes read: %d", n);
	} else {
		/* No body --> stop reading. */
		thread_log(1, 0, "no body");
		return (-1);
	}

	return (connclose);
}

static void *
replay_thread(void *arg)
{
	struct iovec iov[6];
	char space[1] = " ", crlf[2] = "\r\n";
	struct thread *thr = arg;
	struct message *msg;
	enum shmlogtag tag;
	size_t len;
	char *ptr;
	const char *next;

	int i;

	int reopen = 1;

	while ((msg = mailbox_get(&thr->mbox)) != NULL) {
		tag = msg->tag;
		len = msg->len;
		ptr = msg->ptr;

		thread_log(2, 0, "%s(%s)", VSL_tags[tag], msg->ptr);

		switch (tag) {
		case SLT_RxRequest:
			if (thr->method != NULL)
				thr->bogus = 1;
			else
				thr->method = trimline(thr, ptr);
			break;

		case SLT_RxURL:
			if (thr->url != NULL)
				thr->bogus = 1;
			else
				thr->url = trimline(thr, ptr);
			break;

		case SLT_RxProtocol:
			if (thr->proto != NULL)
				thr->bogus = 1;
			else
				thr->proto = trimline(thr, ptr);
			break;

		case SLT_RxHeader:
			if (thr->nhdr >= sizeof thr->hdr / sizeof *thr->hdr) {
				thr->bogus = 1;
			} else {
				thr->hdr[thr->nhdr++] = trimline(thr, ptr);
				if (isprefix(ptr, "connection:", &next))
					thr->conn = trimline(thr, next);
			}
			break;

		default:
			break;
		}

		freez(msg->ptr);
		freez(msg);

		if (tag != SLT_ReqEnd)
			continue;

		if (!thr->method || !thr->url || !thr->proto) {
			thr->bogus = 1;
		} else if (strcmp(thr->method, "GET") != 0 &&
		    strcmp(thr->method, "HEAD") != 0) {
			thr->bogus = 1;
		} else if (strcmp(thr->proto, "HTTP/1.0") == 0) {
			reopen = !(thr->conn &&
			    strcasecmp(thr->conn, "keep-alive") == 0);
		} else if (strcmp(thr->proto, "HTTP/1.1") == 0) {
			reopen = (thr->conn &&
			    strcasecmp(thr->conn, "close") == 0);
		} else {
			thr->bogus = 1;
		}

		if (thr->bogus) {
			thread_log(1, 0, "bogus");
			goto clear;
		}

		if (thr->sock == -1) {
			for (;;) {
				thread_log(1, 0, "sleeping before connect...");
				usleep(1000 * (thr->fd % 3001));
				if ((thr->sock = VSS_connect(addr_info)) >= 0)
					break;
				thread_log(0, errno, "connect failed");
			}
		}

		thread_log(1, 0, "%s %s %s", thr->method, thr->url, thr->proto);

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
			thread_log(0, errno, "writev()");
			goto close;
		}

		for (i = 0; i < thr->nhdr; ++i) {
			thread_log(2, 0, "%d %s", i, thr->hdr[i]);
			iov[0].iov_base = thr->hdr[i];
			iov[0].iov_len = strlen(thr->hdr[i]);
			iov[1].iov_base = crlf;
			iov[1].iov_len = 2;
			if (writev(thr->sock, iov, 2) == -1) {
				thread_log(0, errno, "writev()");
				goto close;
			}
		}
		if (write(thr->sock, crlf, 2) == -1) {
			thread_log(0, errno, "writev()");
			goto close;
		}
		if (receive_response(thr) || reopen) {
close:
			thread_log(1, 0, "close");
			assert(thr->sock != -1);
			close(thr->sock);
			thr->sock = -1;
		}

		sleep(1);
clear:
		/* clean up */
		thread_clear(thr);
	}

	/* leftovers */
	thread_clear(thr);

	return (0);
}

static int
gen_traffic(void *priv, enum shmlogtag tag, unsigned fd,
    unsigned len, unsigned spec, const char *ptr)
{
	struct thread *thr;
	const char *end;
	struct message *msg;

	(void)priv;

	end = ptr + len;

	if (fd == 0 || !(spec & VSL_S_CLIENT))
		return (0);

	thread_log(3, 0, "%d %s", fd, VSL_tags[tag]);
	thr = thread_get(fd, replay_thread);
	if (thr == NULL)
		return (0);
	msg = malloc(sizeof (struct message));
	msg->tag = tag;
	msg->len = len;
	msg->ptr = strndup(ptr, len);
	mailbox_put(&thr->mbox, msg);

	return (0);
}

/* Initiate a connection to <address> by resolving the
 * hostname and returning a struct with necessary
 * connection info.
 */
static struct vss_addr *
init_connection(const char *address)
{
	struct vss_addr **ta;
	struct vss_addr *tap;
	char *addr, *port;
	int i, n;

	if (VSS_parse(address, &addr, &port) != 0) {
		thread_log(0, 0, "Invalid address");
		exit(2);
	}
	n = VSS_resolve(addr, port, &ta);
	free(addr);
	free(port);
	if (n == 0) {
		thread_log(0, 0, "Could not connect to server");
		exit(2);
	}
	for (i = 1; i < n; ++i) {
		free(ta[i]);
		ta[i] = NULL;
	}
	tap = ta[0];
	free(ta);

	return (tap);
}

/*--------------------------------------------------------------------*/

static void
usage(void)
{

	fprintf(stderr,
	    "usage: varnishreplay [-D] -a address:port -r logfile\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int c;
	struct VSL_data *vd;
	const char *address = NULL;

	vd = VSL_New();
	debug = 0;

	VSL_Arg(vd, 'c', NULL);
	while ((c = getopt(argc, argv, "a:Dr:")) != -1) {
		switch (c) {
		case 'a':
			address = optarg;
			break;
		case 'D':
			++debug;
			break;
		default:
			if (VSL_Arg(vd, c, optarg) > 0)
				break;
			usage();
		}
	}

	if (address == NULL) {
		usage();
	}

	if (VSL_OpenLog(vd, NULL))
		exit(1);

	addr_info = init_connection(address);

	signal(SIGPIPE, SIG_IGN);

	pthread_attr_init(&thread_attr);

	/*
	 * XXX: seting the stack size manually reduces the memory usage
	 * XXX: (allowing more threads) and increases speed (?)
	 */
	pthread_attr_setstacksize(&thread_attr, 32768);

	while (VSL_Dispatch(vd, gen_traffic, NULL) == 0)
		/* nothing */ ;
	thread_close(-1);
	exit(0);
}
