/*
 * Copyright (c) 2006-2008 Linpro AS
 * All rights reserved.
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

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "libvarnish.h"
#include "vsb.h"

#include "vtc.h"

#define		MAX_FILESIZE		(1024 * 1024)
#define		MAX_TOKENS		100

const char	*vtc_file;
char		*vtc_desc;

static int	stop;

/**********************************************************************
 * Read a file into memory
 */

static char *
read_file(const char *fn)
{
	char *buf;
	ssize_t sz = MAX_FILESIZE;
	ssize_t s;
	int fd;

	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Cannot open %s: %s", fn, strerror(errno));
		exit (1);
	}
	buf = malloc(sz);
	assert(buf != NULL);
	s = read(fd, buf, sz - 1);
	if (s <= 0) {
		fprintf(stderr, "Cannot read %s: %s", fn, strerror(errno));
		exit (1);
	}
	AZ(close (fd));
	assert(s < sz);		/* XXX: increase MAX_FILESIZE */
	buf[s] = '\0';
	buf = realloc(buf, s + 1);
	assert(buf != NULL);
	return (buf);
}

/**********************************************************************
 * Execute a file
 */

void
parse_string(char *buf, const struct cmds *cmd, void *priv, struct vtclog *vl)
{
	char *token_s[MAX_TOKENS], *token_e[MAX_TOKENS];
	char *p, *q;
	int nest_brace;
	int tn;
	const struct cmds *cp;

	assert(buf != NULL);
	for (p = buf; *p != '\0'; p++) {
		/* Start of line */
		if (isspace(*p))
			continue;
		if (*p == '#') {
			for (; *p != '\0' && *p != '\n'; p++)
				;
			if (*p == '\0')
				break;
			continue;
		}

		/* First content on line, collect tokens */
		tn = 0;
		while (*p != '\0') {
			assert(tn < MAX_TOKENS);
			if (*p == '\n') { /* End on NL */
				break;
			}
			if (isspace(*p)) { /* Inter-token whitespace */
				p++;
				continue;
			}
			if (*p == '\\' && p[1] == '\n') { /* line-cont */
				p += 2;
				continue;
			}
			if (*p == '"') { /* quotes */
				token_s[tn] = ++p;
				q = p;
				for (; *p != '\0'; p++) {
					if (*p == '"')
						break;

					if (*p == '\\' && p[1] == 'n') {
						*q++ = '\n';
						p++;
					} else if (*p == '\\' && p[1] == 'r') {
						*q++ = '\r';
						p++;
					} else if (*p == '\\' && p[1] == '\\') {
						*q++ = '\\';
						p++;
					} else if (*p == '\\' && p[1] == '"') {
						*q++ = '"';
						p++;
					} else {
						if (*p == '\n')
							fprintf(stderr, "Unterminated quoted string in line:\n%s", p);
						assert(*p != '\n');
						*q++ = *p;
					}
				}
				token_e[tn++] = q;
				p++;
			} else if (*p == '{') { /* Braces */
				nest_brace = 0;
				token_s[tn] = p + 1;
				for (; *p != '\0'; p++) {
					if (*p == '{')
						nest_brace++;
					else if (*p == '}') {
						if (--nest_brace == 0)
							break;
					}
				}
				assert(*p == '}');
				token_e[tn++] = p++;
			} else { /* other tokens */
				token_s[tn] = p;
				for (; *p != '\0' && !isspace(*p); p++)
					;
				token_e[tn++] = p;
			}
		}
		assert(tn < MAX_TOKENS);
		token_s[tn] = NULL;
		for (tn = 0; token_s[tn] != NULL; tn++) {
			AN(token_e[tn]);	/*lint !e771 */
			*token_e[tn] = '\0';	/*lint !e771 */
		}

		for (cp = cmd; cp->name != NULL; cp++)
			if (!strcmp(token_s[0], cp->name))
				break;
		if (cp->name == NULL) {
			for (tn = 0; token_s[tn] != NULL; tn++)
				fprintf(stderr, "%s ", token_s[tn]);
			fprintf(stderr, "\n");
			fprintf(stderr, "Unknown command: \"%s\"", token_s[0]);
			exit (1);
		}
	
		assert(cp->cmd != NULL);
		cp->cmd(token_s, priv, cmd, vl);
		if (stop)
			break;
	}
}

/**********************************************************************
 * Reset commands (between tests)
 */

static void
reset_cmds(const struct cmds *cmd)
{

	for (; cmd->name != NULL; cmd++)
		cmd->cmd(NULL, NULL, NULL, NULL);
}

/**********************************************************************
 * Output test description
 */

static void
cmd_test(CMD_ARGS)
{

	(void)priv;
	(void)cmd;
	(void)vl;

	if (av == NULL)
		return;
	assert(!strcmp(av[0], "test"));

	printf("#    TEST %s\n", av[1]);
	AZ(av[2]);
	vtc_desc = strdup(av[1]);
}

/**********************************************************************
 * Shell command execution
 */

static void
cmd_shell(CMD_ARGS)
{

	(void)priv;
	(void)cmd;

	if (av == NULL)
		return;
	AN(av[1]);
	AZ(av[2]);
	vtc_dump(vl, 4, "shell", av[1]);
	(void)system(av[1]);	/* XXX: assert ? */
}

/**********************************************************************
 * Dump command arguments
 */

void
cmd_delay(CMD_ARGS)
{
	double f;

	(void)priv;
	(void)cmd;
	if (av == NULL)
		return;
	AN(av[1]);
	AZ(av[2]);
	f = strtod(av[1], NULL);
	vtc_log(vl, 3, "delaying %g second(s)", f);
	if (f > 100.) {
		(void)sleep((int)f);
	} else {
		(void)usleep((int)(f * 1e6));
	}
}

/**********************************************************************
 * Dump command arguments
 */

void
cmd_dump(CMD_ARGS)
{

	(void)cmd;
	(void)vl;
	if (av == NULL)
		return;
	printf("cmd_dump(%p)\n", priv);
	while (*av)
		printf("\t<%s>\n", *av++);
}

/**********************************************************************
 * Check random generator
 */

#define NRNDEXPECT	12
static const unsigned long random_expect[NRNDEXPECT] = {
	1804289383,	846930886,	1681692777,	1714636915,
	1957747793,	424238335,	719885386,	1649760492,
	 596516649,	1189641421,	1025202362,	1350490027
};

#define RND_NEXT_1K	0x3bdcbe30

static void
cmd_random(CMD_ARGS)
{
	unsigned long l;
	int i;

	(void)cmd;
	(void)priv;
	if (av == NULL)
		return;
	srandom(1);
	for (i = 0; i < NRNDEXPECT; i++) {
		l = random();
		if (l == random_expect[i])
			continue;
		vtc_log(vl, 4, "random[%d] = 0x%x (expect 0x%x)",
		    i, l, random_expect[i]);
		vtc_log(vl, 1, "SKIPPING test: unknown srandom(1) sequence.");
		stop = 1;
		break;
	}
	l = 0;
	for (i = 0; i < 1000; i++) 
		l += random();
	if (l != RND_NEXT_1K) {
		vtc_log(vl, 4, "sum(random[%d...%d]) = 0x%x (expect 0x%x)",
		    NRNDEXPECT, NRNDEXPECT + 1000,
		    l, RND_NEXT_1K);
		vtc_log(vl, 1, "SKIPPING test: unknown srandom(1) sequence.");
		stop = 1;
	}
}

/**********************************************************************
 * Execute a file
 */

static struct cmds cmds[] = {
	{ "server", 	cmd_server },
	{ "client", 	cmd_client },
	{ "varnish", 	cmd_varnish },
	{ "delay", 	cmd_delay },
	{ "test", 	cmd_test },
	{ "shell", 	cmd_shell },
	{ "sema", 	cmd_sema },
	{ "random",	cmd_random },
	{ NULL, 	NULL }
};

static void
exec_file(const char *fn, struct vtclog *vl)
{
	char *buf;

	stop = 0;
	vtc_file = fn;
	vtc_desc = NULL;
	vtc_log(vl, 1, "TEST %s starting", fn);
	buf = read_file(fn);
	parse_string(buf, cmds, NULL, vl);
	vtc_log(vl, 1, "RESETTING after %s", fn);
	reset_cmds(cmds);
	vtc_log(vl, 1, "TEST %s completed", fn);
	vtc_file = NULL;
	free(vtc_desc);
}

/**********************************************************************
 * Print usage
 */

static void
usage(void)
{
	fprintf(stderr, "usage: varnishtest [-n iter] [-qv] file ...\n");
	exit(1);
}

/**********************************************************************
 * Main 
 */

int
main(int argc, char * const *argv)
{
	int ch, i, ntest = 1;
	FILE *fok;
	static struct vtclog	*vl;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	vl = vtc_logopen("top");
	AN(vl);
	while ((ch = getopt(argc, argv, "n:qv")) != -1) {
		switch (ch) {
		case 'n':
			ntest = strtoul(optarg, NULL, 0);
			break;
		case 'q':
			vtc_verbosity--;
			break;
		case 'v':
			vtc_verbosity++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		usage();

	init_sema();
	for (i = 0; i < ntest; i++) {
		for (ch = 0; ch < argc; ch++)
			exec_file(argv[ch], vl);
	}
	fok = fopen("_.ok", "w");
	if (fok != NULL)
		fclose(fok);
	return (0);
}
