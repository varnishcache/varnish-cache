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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "libvarnish.h"
#include "varnishapi.h"
#include "vss.h"

static struct request {
	char *df_H;			/* %H, Protocol version */
	char *df_Host;			/* %{Host}i */
	char *df_Uq;			/* %U%q, URL path and query string */
	char *df_m;			/* %m, Request method*/
	char *df_c;			/* Connection info (keep-alive, close) */
	int bogus;			/* bogus request */
} **req;

static size_t nreq;

static struct vss_addr *adr_info;
static int sock;
static int reopen;
static int debug;

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
static struct vss_addr*
init_connection(const char* address)
{
	struct vss_addr **ta;
	struct vss_addr *tap;
	char *addr, *port;
	int i, n;

	XXXAZ(VSS_parse(address, &addr, &port));
	XXXAN(n = VSS_resolve(addr, port, &ta));
	free(addr);
	free(port);
	if (n == 0) {
		fprintf(stderr, "Could not open TELNET port\n");
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
read_line(char **line)
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
		//fprintf(stderr, "start reading\n");
		i = read(sock, buf + nbuf, 1);
		if (i <= 0) {
			perror("error in reading\n");
			free(buf);
			exit(1);
		}
		nbuf += i;
		if (buf[nbuf-2] == '\r' && buf[nbuf-1] == '\n')
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
read_block(int length)
{
	char *buf;
	int n, nbuf;

	buf = malloc(length);
	nbuf = 0;
	while (nbuf < length) {
		n = read(sock, buf + nbuf,
		    (2048 < length - nbuf ? 2048 : length - nbuf));
		if (n <= 0) {
			perror("failed reading the block\n");
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
receive_response(void)
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
	while (1) {
		line_len = read_line(&line);
		end = line + line_len;

		if (*line == '\r' && *(line + 1) == '\n') {
			free(line);
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

		free(line);
	}

	if (debug)
		fprintf(stderr, "status: %d\n", status);


	/* Read body */
	if (content_length > 0 && !chunked) {
		/* Fixed body size, read content_length bytes */
		if (debug)
			fprintf(stderr, "fixed length\n");
		n = read_block(content_length);
		if (debug) {
			fprintf(stderr, "size of body: %d\n", (int)content_length);
			fprintf(stderr, "bytes read: %d\n", n);
		}
	} else if (chunked) {
		/* Chunked encoding, read size and bytes until no more */
		if (debug)
			fprintf(stderr, "chunked encoding\n");
		while (1) {
			line_len = read_line(&line);
			end = line + line_len;
			block_len = strtol(line, &end, 16);
			if (block_len == 0) {
				break;
			}
			n = read_block(block_len);
			if (debug) {
				fprintf(stderr, "size of body: %d\n", (int)block_len);
				fprintf(stderr, "bytes read: %d\n", n);
			}
			free(line);
			n = read_line(&line);
			free(line);
		}
		n = read_line(&line);
		free(line);
	} else if ((content_length <= 0 && !chunked) || req_failed) {
		/* No body --> stop reading. */
		if (debug)
			fprintf(stderr, "no body\n");
	} else {
		/* Unhandled case. */
		fprintf(stderr, "An error occured\n");
		exit(1);
	}
	if (debug)
		fprintf(stderr, "\n");

	return close_connection;
}


static int
gen_traffic(void *priv, enum shmlogtag tag, unsigned fd,
    unsigned len, unsigned spec, const char *ptr)
{
	const char *end, *next;
	FILE *fo;
	struct request *rp;

	end = ptr + len;

	if (!(spec & VSL_S_CLIENT))
		return (0);

	if (fd >= nreq) {
		struct request **newreq = req;
		size_t newnreq = nreq;

		while (fd >= newnreq)
			newnreq += newnreq + 1;
		newreq = realloc(newreq, newnreq * sizeof *newreq);
		assert(newreq != NULL);
		memset(newreq + nreq, 0, (newnreq - nreq) * sizeof *newreq);
		req = newreq;
		nreq = newnreq;
	}
	if (req[fd] == NULL) {
		req[fd] = calloc(sizeof *req[fd], 1);
		assert(req[fd] != NULL);
	}
	rp = req[fd];

	switch (tag) {
	case SLT_RxRequest:
		if (tag == SLT_RxRequest && (spec & VSL_S_BACKEND))
			break;

		if (rp->df_m != NULL)
			rp->bogus = 1;
		else
			rp->df_m = trimline(ptr, end);
		break;

	case SLT_RxURL:
		if (tag == SLT_RxURL && (spec & VSL_S_BACKEND))
			break;

		if (rp->df_Uq != NULL)
			rp->bogus = 1;
		else
			rp->df_Uq = trimline(ptr, end);
		break;

	case SLT_RxProtocol:
		if (tag == SLT_RxProtocol && (spec & VSL_S_BACKEND))
			break;

		if (rp->df_H != NULL)
			rp->bogus = 1;
		else
			rp->df_H = trimline(ptr, end);
		break;

	case SLT_RxHeader:
		if (isprefix(ptr, "host:", end, &next))
			rp->df_Host = trimline(next, end);
		if (isprefix(ptr, "connection:", end, &next))
			rp->df_c = trimline(next, end);
		break;

	default:
		break;
	}

	if ((spec & VSL_S_CLIENT) && tag != SLT_ReqEnd)
		return (0);

	if (!rp->bogus) {
		fo = priv;

		/* If the method is supported (GET or HEAD), send the request out
		 * on the socket. If the socket needs reopening, reopen it first.
		 * When the request is sent, call the function for receiving
		 * the answer.
		 */
		if (!(strncmp(rp->df_m, "GET", 3) && strncmp(rp->df_m, "HEAD", 4))) {
			if (reopen)
				sock = VSS_connect(adr_info);
			reopen = 0;

			if (debug) {
				fprintf(fo, "%s ", rp->df_m);
				fprintf(fo, "%s ", rp->df_Uq);
				fprintf(fo, "%s ", rp->df_H);
				fprintf(fo, "\n");
				fprintf(fo, "Host: ");
			}
			write(sock, rp->df_m, strlen(rp->df_m));
			write(sock, " ", 1);
			write(sock, rp->df_Uq, strlen(rp->df_Uq));
			write(sock, " ", 1);
			write(sock, rp->df_H, strlen(rp->df_H));
			write(sock, " ", 1);
			write(sock, "\r\n", 2);

			if (strncmp(rp->df_H, "HTTP/1.0", 8))
				reopen = 1;

			write(sock, "Host: ", 6);
			if (rp->df_Host) {
				if (debug)
					fprintf(fo, rp->df_Host);
				write(sock, rp->df_Host, strlen(rp->df_Host));
			}
			if (debug)
				fprintf(fo, "\n");
			write(sock, "\r\n", 2);
			if (rp->df_c) {
				if (debug)
					fprintf(fo, "Connection: %s\n", rp->df_c);
				write(sock, "Connection: ", 12);
				write(sock, rp->df_c, strlen(rp->df_c));
				write(sock, "\r\n", 2);
				if (isequal(rp->df_c, "keep-alive", rp->df_c + strlen(rp->df_c)))
					reopen = 0;
			}
			if (debug)
				fprintf(fo, "\n");
			write(sock, "\r\n", 2);
			if (!reopen)
				reopen = receive_response();
			if (reopen)
				close(sock);
		}
	}

	/* clean up */
#define freez(x) do { if (x) free(x); x = NULL; } while (0);
	freez(rp->df_H);
	freez(rp->df_Host);
	freez(rp->df_Uq);
	freez(rp->df_m);
	freez(rp->df_c);
#undef freez
	rp->bogus = 0;

	return (0);
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
	adr_info = init_connection(address);
	sock = VSS_connect(adr_info);
	while (read(fd, buf, 1)) {
		write(sock, buf, 1);
		fprintf(stderr, "%s", buf);
		if (*buf == '\n' && last == '\n'){
			fprintf(stderr, "receive\n");
			reopen = receive_response();
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
	int i, c;
	struct VSL_data *vd;
	const char *ofn = NULL;
	const char *address = NULL;
	FILE *of;

	char *test_file = NULL;

	vd = VSL_New();
	debug = 0;

	while ((c = getopt(argc, argv, "a:Dr:t:")) != -1) {
		i = VSL_Arg(vd, c, optarg);
		if (i < 0)
			exit (1);
		if (i > 0)
			continue;
		switch (c) {
		case 'a':
			address = optarg;
			break;
		case 'D':
			debug = 1;
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

	ofn = "stdout";
	of = stdout;

	adr_info = init_connection(address);
	reopen = 1;

	while (VSL_Dispatch(vd, gen_traffic, of) == 0) {
		if (fflush(of) != 0) {
			perror(ofn);
			exit(1);
		}
	}

	exit(0);
}
