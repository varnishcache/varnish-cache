/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * const char **ParseArgv(const char *s, int comment)
 *	Parse a command like line into an argv[]
 *	Index zero contains NULL or an error message
 *	"double quotes" and backslash substitution is handled.
 *
 * void FreeArgv(const char **argv)
 *	Free the result of ParseArgv()
 *
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "libvarnish.h"

static int
BackSlash(const char *s, char *res)
{
	int r;
	char c;
	unsigned u;

	assert(*s == '\\');
	r = c = 0;
	switch(s[1]) {
	case 'n':
		c = '\n';
		r = 2;
		break;
	case 'r':
		c = '\r';
		r = 2;
		break;
	case 't':
		c = '\t';
		r = 2;
		break;
	case '"':
		c = '"';
		r = 2;
		break;
	case '\\':
		c = '\\';
		r = 2;
		break;
	case '0': case '1': case '2': case '3':
	case '4': case '5': case '6': case '7':
		for (r = 1; r < 4; r++) {
			if (!isdigit(s[r]))
				break;
			if (s[r] - '0' > 7)
				break;
			c <<= 3;	/*lint !e701 signed left shift */
			c |= s[r] - '0';
		}
		break;
	case 'x':
		if (1 == sscanf(s + 1, "x%02x", &u)) {
			assert(!(u & ~0xff));
			c = u;	/*lint !e734 loss of precision */
			r = 4;
		}
		break;
	default:
		break;
	}
	if (res != NULL)
		*res = c;
	return (r);
}

static char *
BackSlashDecode(const char *s, const char *e)
{
	const char *q;
	char *p, *r;
	int i;

	p = calloc((e - s) + 1, 1);
	if (p == NULL)
		return (p);
	for (r = p, q = s; q < e; ) {
		if (*q != '\\') {
			*r++ = *q++;
			continue;
		}
		i = BackSlash(q, r);
		q += i;
		r++;
	}
	*r = '\0';
	return (p);
}

static char err_invalid_backslash[] = "Invalid backslash sequence";
static char err_missing_quote[] = "Missing '\"'";

char **
ParseArgv(const char *s, int flag)
{
	char **argv;
	const char *p;
	int nargv, largv;
	int i, quote;

	assert(s != NULL);
	nargv = 1;
	largv = 16;
	argv = calloc(sizeof *argv, largv);
	if (argv == NULL)
		return (NULL);

	for (;;) {
		if (*s == '\0')
			break;
		if (isspace(*s)) {
			s++;
			continue;
		}
		if ((flag & ARGV_COMMENT) && *s == '#')
			break;
		if (*s == '"') {
			p = ++s;
			quote = 1;
		} else {
			p = s;
			quote = 0;
		}
		while (1) {
			if (*s == '\\') {
				i = BackSlash(s, NULL);
				if (i == 0) {
					argv[0] = err_invalid_backslash;
					return (argv);
				}
				s += i;
				continue;
			}
			if (!quote) {
				if (*s == '\0' || isspace(*s))
					break;
				if ((flag & ARGV_COMMA) && *s == ',')
					break;
				s++;
				continue;
			}
			if (*s == '"')
				break;
			if (*s == '\0') {
				argv[0] = err_missing_quote;
				return (argv);
			}
			s++;
		}
		if (nargv + 1 >= largv) {
			argv = realloc(argv, sizeof (*argv) * (largv += largv));
			assert(argv != NULL);
		}
		argv[nargv++] = BackSlashDecode(p, s);
		if (*s != '\0')
			s++;
	}
	argv[nargv++] = NULL;
	return (argv);
}

void
FreeArgv(char **argv)
{
	int i;

	for (i = 1; argv[i] != NULL; i++)
		free(argv[i]);
	free(argv);
}

#ifdef TESTPROG

#include <printf.h>

static void
PrintArgv(char **argv)
{
	int i;

	printf("---- %p\n", argv);
	if (argv[0] != NULL)
		printf("err %V\n", argv[0]);
	for (i = 1; argv[i] != NULL; i++)
		printf("%3d %V\n", i, argv[i]);
}

static void
Test(const char *str)
{
	char **av;

	printf("Test: <%V>\n", str);
	av = ParseArgv(str, 0);
	PrintArgv(av);
}

int
main(int argc, char **argv)
{
	char buf[BUFSIZ];

	(void)argc;
	(void)argv;

	register_printf_render_std("V");

	while (fgets(buf, sizeof buf, stdin))
		Test(buf);

	return (0);
}
#endif
