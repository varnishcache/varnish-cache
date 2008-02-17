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
	return msg;
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
thread_log(int lvl, const char *fmt, ...)
{
	va_list ap;

	if (lvl > debug)
		return;
	pthread_mutex_lock(&log_mutex);
	fprintf(stderr, "%p ", (void *)pthread_self());
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
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
			thread_log(0, "thread creation failed\n");
			mailbox_destroy(&threads[fd]->mbox);
			freez(threads[fd]);
		}
		thread_log(1, "thread %p started\n",
		    (void *)threads[fd]->thread_id);
	}
	return (threads[fd]);
}

static void
thread_close(int fd)
{

	assert(fd < nthreads);
	if (fd == 0) {
		for (fd = 1; fd < nthreads; ++fd)
			thread_close(fd);
		return;
	}

	if (threads[fd] == NULL)
		return;
	mailbox_close(&threads[fd]->mbox);
	pthread_join(threads[fd]->thread_id, NULL);
	thread_log(1, "thread %p stopped\n",
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
		thread_log(0, "Invalid address\n");
		exit(2);
	}
	n = VSS_resolve(addr, port, &ta);
	free(addr);
	free(port);
	if (n == 0) {
		thread_log(0, "Could not connect to server\n");
		exit(2);
	}
	for (i = 1; i < n; ++i) {
		free(ta[i]);
		ta[i] = NULL;
	}
	tap = ta[0];
	free(ta);

	return tap;
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
		if ((nbuf + 2) >= lbuf) {
			lbuf += lbuf;
			buf = realloc(buf, lbuf);
			XXXAN(buf);
		}
		i = read(sock, buf + nbuf, 1);
		if (i <= 0) {
			thread_log(0, "read(): %s\n", strerror(errno));
			free(buf);
			return (-1);
		}
		nbuf += i;
		if (nbuf >= 2 && buf[nbuf-2] == '\r' && buf[nbuf-1] == '\n')
			break;

	}
	buf[nbuf] = '\0';
	*line = buf;
	return nbuf+1;
}

/* Read a block of data from the socket, and do nothing with it.
 * length says how many bytes to read, and the function returns
 * the number of bytes read.
 */
static int
read_block(int length, int sock)
{
	char *buf;
	int n, nbuf;

	buf = malloc(length);
	nbuf = 0;
	while (nbuf < length) {
		n = read(sock, buf + nbuf,
		    (2048 < length - nbuf ? 2048 : length - nbuf));
		if (n <= 0) {
			thread_log(0, "failed reading the block\n");
			nbuf = -1;
			break;
		}
		nbuf += n;
	}
	free(buf);
	return nbuf;
}

/* Receive the response after sending a request.
 */
static int
receive_response(int sock)
{
	char *line, *end;
	const char *next;
	int line_len;
	long content_length = -1;
	int chunked = 0;
	int close_connection = 0;
	int req_failed = 1;
	int n;
	long block_len;
	int status;

	/* Read header */
	for (;;) {
		line_len = read_line(&line, sock);
		if (line_len < 0)
			return (-1);
		end = line + line_len;

		if (line_len >= 2 && line[0] == '\r' && line[1] == '\n') {
			freez(line);
			break;
		}

		if (strncmp(line, "HTTP", 4) == 0) {
			sscanf(line, "%*s %d %*s\r\n", &status);
			req_failed = (status != 200);
		} else if (isprefix(line, "content-length:", end, &next))
			content_length = strtol(next, &end, 10);
		else if (isprefix(line, "encoding:", end, &next) ||
		    isprefix(line, "transfer-encoding:", end, &next))
			chunked = (strstr(next, "chunked") != NULL);
		else if (isprefix(line, "connection:", end, &next))
			close_connection = (strstr(next, "close") != NULL);

		freez(line);
	}

	thread_log(1, "status: %d\n", status);


	/* Read body */
	if (content_length > 0 && !chunked) {
		/* Fixed body size, read content_length bytes */
		thread_log(1, "fixed length\n");
		thread_log(1, "size of body: %ld\n", content_length);
		if ((n = read_block(content_length, sock)) < 0)
			return (1);
		thread_log(1, "bytes read: %d\n", n);
	} else if (chunked) {
		/* Chunked encoding, read size and bytes until no more */
		thread_log(1, "chunked encoding\n");
		for (;;) {
			if ((line_len = read_line(&line, sock)) < 0)
				return (-1);
			end = line + line_len;
			block_len = strtol(line, &end, 16);
			freez(line);
			if (block_len == 0)
				break;
			if ((n = read_block(block_len, sock)) < 0)
				return (-1);
			thread_log(1, "size of body: %d\n", (int)block_len);
			thread_log(1, "bytes read: %d\n", n);
			if ((n = read_line(&line, sock)) < 0)
				return (-1);
			freez(line);
		}
		n = read_line(&line, sock);
		freez(line);
	} else if ((content_length <= 0 && !chunked) || req_failed) {
		/* No body --> stop reading. */
		thread_log(1, "no body\n");
		return (-1);
	} else {
		/* Unhandled case. */
		thread_log(0, "An error occured\n");
		return (-1);
	}

	return close_connection;
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

		thread_log(2, "%s(%s)\n", VSL_tags[tag], msg->ptr);

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
			thread_log(1, "bogus\n");
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

				thread_log(1, "%s %s %s\n", df_m, df_Uq, df_H);

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
					thread_log(1, "Host: %s\n", df_Host);
					write(sock, df_Host, strlen(df_Host));
				}
				write(sock, "\r\n", 2);
				if (df_c) {
					thread_log(1, "Connection: %s\n", df_c);
					write(sock, "Connection: ", 12);
					write(sock, df_c, strlen(df_c));
					write(sock, "\r\n", 2);
					if (isequal(df_c, "keep-alive", df_c + strlen(df_c)))
						reopen = 0;
				}
				if (debug)
					thread_log(0, "\n");
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

	thread_log(2, "%d %s\n", fd, VSL_tags[tag]);
	thr = thread_get(fd, replay_thread);
	msg = malloc(sizeof (struct message));
	msg->tag = tag;
	msg->len = len;
	msg->ptr = strndup(ptr, len);
	mailbox_put(&thr->mbox, msg);

	return 0;
}


/* This function is for testing only, and only sends
 * the raw data from the file to the address.
 * The receive function is called for each blank line.
 */
static void
send_test_request(char *file, const char *address)
{
	int fd = open(file, O_RDONLY);
	char buf[2];
	char last = ' ';
	int sock, reopen = 1;

	addr_info = init_connection(address);
	sock = VSS_connect(addr_info);
	while (read(fd, buf, 1)) {
		write(sock, buf, 1);
		thread_log(0, "%s", buf);
		if (*buf == '\n' && last == '\n'){
			thread_log(0, "receive\n");
			reopen = receive_response(sock);
		}
		last = *buf;
	}
	close(sock);

}

/*--------------------------------------------------------------------*/

static void
usage(void)
{

	fprintf(stderr, "usage: varnishreplay -a address:port -r logfile [-D]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int c;
	struct VSL_data *vd;
	const char *address = NULL;

	char *test_file = NULL;

	vd = VSL_New();
	debug = 0;

	VSL_Arg(vd, 'c', NULL);
	while ((c = getopt(argc, argv, "a:Dr:t:")) != -1) {
		switch (c) {
		case 'a':
			address = optarg;
			break;
		case 'D':
			++debug;
			break;
		case 't':
			/* This option is for testing only. The test file must contain
			 * a sequence of valid HTTP-requests that can be sent
			 * unchanged to the adress given with -a
			 */
			test_file = optarg;
			break;
		default:
			if (VSL_Arg(vd, c, optarg) > 0)
				break;
			usage();
		}
	}

	if (test_file != NULL) {
		send_test_request(test_file, address);
		exit(0);
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
