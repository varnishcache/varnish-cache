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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libvarnish.h"
#include "vqueue.h"
#include "varnishapi.h"
#include "vss.h"

#ifndef HAVE_STRNDUP
#include "compat/strndup.h"
#endif

#define freez(x) do { if (x) free(x); x = NULL; } while (0);

static struct vss_addr *addr_info;
static int debug;

/*
 * mailbox toolkit
 */

struct message {
	enum shmlogtag tag;
	size_t len;
	char *ptr;
	VSTAILQ_ENTRY(message) list;
};

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

struct thread {
	pthread_t thread_id;
	struct mailbox mbox;
};

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

static struct thread **threads;
static size_t nthreads;

static struct thread *
thread_get(int fd, void *(*thread_main)(void *))
{

	assert(fd != 0);
	if (fd >= nthreads) {
		struct thread **newthreads = threads;
		size_t newnthreads = nthreads;

		while (fd >= newnthreads)
			newnthreads += newnthreads + 1;
		newthreads = realloc(newthreads, newnthreads * sizeof *newthreads);
		assert(newthreads != NULL);
		memset(newthreads + nthreads, 0,
		    (newnthreads - nthreads) * sizeof *newthreads);
		threads = newthreads;
		nthreads = newnthreads;
	}
	if (threads[fd] == NULL) {
		threads[fd] = malloc(sizeof *threads[fd]);
		assert(threads[fd] != NULL);
		mailbox_create(&threads[fd]->mbox);
		if (pthread_create(&threads[fd]->thread_id, NULL,
		    thread_main, threads[fd]) != 0) {
			thread_log(0, errno, "pthread_create()");
			mailbox_destroy(&threads[fd]->mbox);
			freez(threads[fd]);
		}
		thread_log(1, 0, "thread %p started",
		    (void *)threads[fd]->thread_id);
	}
	return (threads[fd]);
}

static void
thread_close(int fd)
{

	assert(fd == 0 || fd < nthreads);
	if (fd == 0) {
		for (fd = 1; fd < nthreads; ++fd)
			thread_close(fd);
		return;
	}

	if (threads[fd] == NULL)
		return;
	mailbox_close(&threads[fd]->mbox);
	pthread_join(threads[fd]->thread_id, NULL);
	thread_log(1, 0, "thread %p stopped",
	    (void *)threads[fd]->thread_id);
	mailbox_destroy(&threads[fd]->mbox);
	freez(threads[fd]);
}

/*
 * ...
 */

static int
isprefix(const char *str, const char *prefix, const char *end, const char **next)
{

	while (str < end && *str && *prefix &&
	    tolower((int)*str) == tolower((int)*prefix))
		++str, ++prefix;
	if (*str && *str != ' ')
		return (0);
	if (next) {
		while (str < end && *str && *str == ' ')
			++str;
		*next = str;
	}
	return (1);
}

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

/*
 * Returns a copy of the entire string with leading and trailing spaces
 * trimmed.
 */
static char *
trimline(const char *str, const char *end)
{
	size_t len;
	char *p;

	/* skip leading space */
	while (str < end && *str && *str == ' ')
		++str;

	/* seek to end of string */
	for (len = 0; &str[len] < end && str[len]; ++len)
		 /* nothing */ ;

	/* trim trailing space */
	while (len && str[len - 1] == ' ')
		--len;

	/* copy and return */
	p = malloc(len + 1);
	assert(p != NULL);
	memcpy(p, str, len);
	p[len] = '\0';
	return (p);
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

/* Read a line from the socket and return the number of bytes read.
 * After returning, line will point to the read bytes in memory.
 * A line is terminated by \r\n
 */
static int
read_line(char **line, int sock)
{
	char *buf;
	unsigned nbuf, lbuf;
	int i;

	lbuf = 4096;
	buf = malloc(lbuf);
	XXXAN(buf);
	nbuf = 0;
	while (1) {
		if (nbuf + 2 >= lbuf) {
			lbuf += lbuf;
			buf = realloc(buf, lbuf);
			XXXAN(buf);
		}
		i = read(sock, buf + nbuf, 1);
		if (i < 0) {
			thread_log(0, errno, "read(%d, %p, %d)",
			    sock, buf + nbuf, 1);
			free(buf);
			return (-1);
		}
		if (i == 0) {
			buf[nbuf] = '\0';
			break;
		}
		nbuf += i;
		if (nbuf >= 2 && buf[nbuf-2] == '\r' && buf[nbuf-1] == '\n') {
			buf[nbuf-2] = '\0';
			break;
		}

	}
	*line = buf;
	return (nbuf - 2);
}

/* Read a block of data from the socket, and do nothing with it.
 * length says how many bytes to read, and the function returns
 * the number of bytes read.
 */
static int
read_block(int length, int sock)
{
	char *buf;
	int len, n, nbuf;

	buf = malloc(length);
	nbuf = 0;
	while (nbuf < length) {
		len = 2048 < length - nbuf ? 2048 : length - nbuf;
		n = read(sock, buf + nbuf, len);
		if (n < 0) {
			thread_log(0, errno, "read(%d, %p, %d)",
			    sock, buf + nbuf, len);
			nbuf = -1;
			break;
		}
		if (n == 0)
			break;
		nbuf += n;
	}
	free(buf);
	return (nbuf);
}

/* Receive the response after sending a request.
 */
static int
receive_response(int sock)
{
	char *line, *end;
	const char *next;
	int line_len;
	long chunk_length, content_length;
	int chunked, connclose, failed;
	int n, status;

	content_length = 0;
	chunked = connclose = failed = 0;

	/* Read header */
	for (;;) {
		line_len = read_line(&line, sock);
		if (line_len < 0)
			return (-1);
		end = line + line_len;
		if (line_len == 0) {
			freez(line);
			break;
		}
		if (strncmp(line, "HTTP", 4) == 0) {
			sscanf(line, "%*s %d %*s\r\n", &status);
			failed = (status != 200);
		} else if (isprefix(line, "content-length:", end, &next)) {
			content_length = strtol(next, NULL, 10);
		} else if (isprefix(line, "transfer-encoding:", end, &next)) {
			chunked = (strcasecmp(next, "chunked") == 0);
		} else if (isprefix(line, "connection:", end, &next)) {
			connclose = (strcasecmp(next, "close") == 0);
		}
		freez(line);
	}

	thread_log(1, 0, "status: %d", status);

	/* Read body */
	if (chunked) {
		/* Chunked encoding, read size and bytes until no more */
		thread_log(1, 0, "chunked encoding");
		for (;;) {
			if ((line_len = read_line(&line, sock)) < 0)
				return (-1);
			end = line + line_len;
			/* read_line() guarantees null-termination */
			chunk_length = strtol(line, NULL, 16);
			freez(line);
			if (chunk_length == 0)
				break;
			if ((n = read_block(chunk_length, sock)) < 0)
				return (-1);
			if (n < chunk_length)
				thread_log(0, 0, "short read: %d/%ld",
				    n, chunk_length);
			thread_log(1, 0, "chunk length: %ld", chunk_length);
			thread_log(1, 0, "bytes read: %d", n);
			/* trainling CR LF */
			if ((n = read_line(&line, sock)) < 0)
				return (-1);
			freez(line);
		}
		/* trailing CR LF */
		n = read_line(&line, sock);
		freez(line);
	} else if (content_length > 0) {
		/* Fixed body size, read content_length bytes */
		thread_log(1, 0, "fixed length");
		thread_log(1, 0, "content length: %ld", content_length);
		if ((n = read_block(content_length, sock)) < 0)
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
	struct thread *thr = arg;
	struct message *msg;
	enum shmlogtag tag;
	size_t len;
	char *ptr;
	const char *end, *next;

	char *df_H = NULL;			/* %H, Protocol version */
	char *df_Host = NULL;			/* %{Host}i */
	char *df_Uq = NULL;			/* %U%q, URL path and query string */
	char *df_m = NULL;			/* %m, Request method*/
	char *df_c = NULL;			/* Connection info (keep-alive, close) */
	int bogus = 0;				/* bogus request */

	int sock, reopen = 1;

	while ((msg = mailbox_get(&thr->mbox)) != NULL) {
		tag = msg->tag;
		len = msg->len;
		ptr = msg->ptr;
		end = ptr + len;

		thread_log(2, 0, "%s(%s)", VSL_tags[tag], msg->ptr);

		switch (tag) {
		case SLT_RxRequest:
			if (df_m != NULL)
				bogus = 1;
			else
				df_m = trimline(ptr, end);
			break;

		case SLT_RxURL:
			if (df_Uq != NULL)
				bogus = 1;
			else
				df_Uq = trimline(ptr, end);
			break;

		case SLT_RxProtocol:
			if (df_H != NULL)
				bogus = 1;
			else
				df_H = trimline(ptr, end);
			break;

		case SLT_RxHeader:
			if (isprefix(ptr, "host:", end, &next))
				df_Host = trimline(next, end);
			if (isprefix(ptr, "connection:", end, &next))
				df_c = trimline(next, end);
			break;

		default:
			break;
		}

		if (tag != SLT_ReqEnd)
			continue;

		if (!df_m || !df_Uq || !df_H)
			bogus = 1;

		if (bogus) {
			thread_log(1, 0, "bogus");
		} else {
			/* If the method is supported (GET or HEAD), send the request out
			 * on the socket. If the socket needs reopening, reopen it first.
			 * When the request is sent, call the function for receiving
			 * the answer.
			 */
			if (!(strcmp(df_m, "GET") && strcmp(df_m, "HEAD"))) {
				if (reopen)
					sock = VSS_connect(addr_info);
				reopen = 0;

				thread_log(1, 0, "%s %s %s", df_m, df_Uq, df_H);

				write(sock, df_m, strlen(df_m));
				write(sock, " ", 1);
				write(sock, df_Uq, strlen(df_Uq));
				write(sock, " ", 1);
				write(sock, df_H, strlen(df_H));
				write(sock, " ", 1);
				write(sock, "\r\n", 2);

				if (strncmp(df_H, "HTTP/1.0", 8))
					reopen = 1;

				write(sock, "Host: ", 6);
				if (df_Host) {
					thread_log(1, 0, "Host: %s", df_Host);
					write(sock, df_Host, strlen(df_Host));
				}
				write(sock, "\r\n", 2);
				if (df_c) {
					thread_log(1, 0, "Connection: %s", df_c);
					write(sock, "Connection: ", 12);
					write(sock, df_c, strlen(df_c));
					write(sock, "\r\n", 2);
					if (isequal(df_c, "keep-alive", df_c + strlen(df_c)))
						reopen = 0;
				}
				write(sock, "\r\n", 2);
				if (!reopen)
					reopen = receive_response(sock);
				if (reopen)
					close(sock);
			}
		}

		/* clean up */
		freez(msg->ptr);
		freez(msg);
		freez(df_H);
		freez(df_Host);
		freez(df_Uq);
		freez(df_m);
		freez(df_c);
		bogus = 0;
	}

	/* leftovers */
	freez(msg->ptr);
	freez(msg);
	freez(df_H);
	freez(df_Host);
	freez(df_Uq);
	freez(df_m);
	freez(df_c);

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

	thread_log(2, 0, "%d %s", fd, VSL_tags[tag]);
	thr = thread_get(fd, replay_thread);
	msg = malloc(sizeof (struct message));
	msg->tag = tag;
	msg->len = len;
	msg->ptr = strndup(ptr, len);
	mailbox_put(&thr->mbox, msg);

	return (0);
}

/*--------------------------------------------------------------------*/

static void
usage(void)
{

	fprintf(stderr, "usage: varnishreplay [-D] -a address:port -r logfile\n");
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

	while (VSL_Dispatch(vd, gen_traffic, NULL) == 0)
		/* nothing */ ;
	thread_close(0);
	exit(0);
}
