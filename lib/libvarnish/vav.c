/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
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
 * const char **VAV_Parse(const char *s, int *argc, int flag)
 *	Parse a command like line into an argv[]
 *	Index zero contains NULL or an error message
 *	(The error message is a static const char* and does not
 *	need saving or copying.)
 *	"double quotes" and backslash substitution is handled.
 *
 * void VAV_Free(const char **argv)
 *	Free the result of VAV_Parse()
 *
 */

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vdef.h"

#include "vas.h"
#include "vav.h"

static int
vav_backslash_txt(const char *s, const char *e, char *res)
{
	int r, l;
	char c;
	unsigned u;

	AN(s);
	if (e == NULL)
		e = strchr(s, '\0');

	l = pdiff(s, e);
	if (l < 2)
		return (0);

	assert(*s == '\\');
	r = c = 0;
	switch (s[1]) {
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
		for (r = 1; r < 4 && r < l; r++) {
			if (!isdigit(s[r]))
				break;
			if (s[r] - '0' > 7)
				break;
			c <<= 3;	/*lint !e701 signed left shift */
			c |= s[r] - '0';
		}
		break;
	case 'x':
		if (l >= 4 && isxdigit(s[2]) && isxdigit(s[3]) &&
		    sscanf(s + 1, "x%02x", &u) == 1) {
			AZ(u & ~0xff);
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

int
VAV_BackSlash(const char *s, char *res)
{

	return (vav_backslash_txt(s, NULL, res));
}

char *
VAV_BackSlashDecode(const char *s, const char *e)
{
	const char *q;
	char *p, *r;
	int i;

	if (e == NULL)
		e = strchr(s, '\0');
	assert(e != NULL);
	p = calloc(1, (e - s) + 1L);
	if (p == NULL)
		return (p);
	for (r = p, q = s; q < e; ) {
		if (*q != '\\') {
			*r++ = *q++;
			continue;
		}
		i = vav_backslash_txt(q, e, r);
		if (i == 0) {
			free(p);
			errno = EINVAL;
			return (NULL);
		}
		q += i;
		r++;
	}
	*r = '\0';
	return (p);
}

static char err_invalid_backslash[] = "Invalid backslash sequence";
static char err_invalid_quote[] = "Invalid '\"'";
static char err_missing_quote[] = "Missing '\"'";
static char err_missing_separator[] = "Missing separator between arguments";

char **
VAV_ParseTxt(const char *b, const char *e, int *argc, int flag)
{
	char **argv;
	const char *p, *sep;
	int nargv, largv;
	int i, quote;

	AN(b);
	if (e == NULL)
		e = strchr(b, '\0');
	sep = NULL;
	quote = 0;
	nargv = 1;
	largv = 16;
	argv = calloc(largv, sizeof *argv);
	if (argv == NULL)
		return (NULL);

	while (b < e) {
		if (isspace(*b)) {
			b++;
			continue;
		}
		if (sep != NULL && isspace(*sep) &&
		    *b == ',' && (flag & ARGV_COMMA)) {
			sep = NULL;
			b++;
			continue;
		}
		if (sep != NULL && *sep == '"' && *b == '"') {
			argv[0] = err_missing_separator;
			return (argv);
		}
		sep = NULL;
		if ((flag & ARGV_COMMENT) && *b == '#')
			break;
		if (*b == '"' && !(flag & ARGV_NOESC)) {
			p = ++b;
			quote = 1;
		} else {
			p = b;
			quote = 0;
		}
		while (b < e) {
			if (*b == '\\' && !(flag & ARGV_NOESC)) {
				i = vav_backslash_txt(b, e, NULL);
				if (i == 0) {
					argv[0] = err_invalid_backslash;
					return (argv);
				}
				b += i;
				continue;
			}
			if (!quote) {
				if (isspace(*b)) {
					sep = b;
					break;
				}
				if ((flag & ARGV_COMMA) && *b == ',') {
					sep = b;
					break;
				}
				if (!(flag & ARGV_NOESC) && *b == '"') {
					argv[0] = err_invalid_quote;
					return (argv);
				}
				b++;
				continue;
			}
			if (*b == '"' && !(flag & ARGV_NOESC)) {
				sep = b;
				quote = 0;
				break;
			}
			b++;
		}
		if (sep == NULL && quote) {
			argv[0] = err_missing_quote;
			return (argv);
		}
		/* Ensure slots for 1 new arg plus 1 trailing arg */
		if (nargv + 2 >= largv) {
			argv = realloc(argv, sizeof (*argv) * (largv += largv));
			assert(argv != NULL);
		}
		if (flag & ARGV_NOESC) {
			argv[nargv] = malloc(1L + (b - p));
			assert(argv[nargv] != NULL);
			memcpy(argv[nargv], p, b - p);
			argv[nargv][b - p] = '\0';
		} else {
			argv[nargv] = VAV_BackSlashDecode(p, b);
			assert(argv[nargv] != NULL);
		}
		nargv++;
		if (b < e)
			b++;
	}
	if (sep != NULL && *sep == ',') {
		argv[nargv] = strdup("");
		assert(argv[nargv] != NULL);
		nargv++;
	}
	argv[nargv] = NULL;
	if (argc != NULL)
		*argc = nargv;
	return (argv);
}

char **
VAV_Parse(const char *s, int *argc, int flag)
{

	return (VAV_ParseTxt(s, NULL, argc, flag));
}

void
VAV_Free(char **argv)
{
	int i;

	for (i = 1; argv[i] != NULL; i++)
		free(argv[i]);
	free(argv);
}

#ifdef TESTPROG

#include <printf.h>

static void
VAV_Print(char **argv)
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
	av = VAV_Parse(str, NULL, 0);
	VAV_Print(av);
}

#if defined __linux__
int
printf_v(FILE *stream, const struct printf_info *info,
    const void *const *args)
{
	const char *v = *((char **)args[0]);
	return (fprintf(stream, "%*s",
	    info->left ? -info->width : info->width, v));
}

int
printf_v_info(const struct printf_info *info, size_t n, int *argtypes,
    int *size)
{
	if (n > 0)
		argtypes[0] = PA_STRING;
	return (1);
}
#endif

int
main(int argc, char **argv)
{
	char buf[BUFSIZ];

	(void)argc;
	(void)argv;

#if defined __FreeBSD__
	register_printf_render_std("V");
#elif defined __linux__
	register_printf_specifier('V', printf_v, printf_v_info);
#else
#error Unsupported platform
#endif

	while (fgets(buf, sizeof buf, stdin))
		Test(buf);

	return (0);
}
#endif

#ifdef TEST_DRIVER
#  include <stdio.h>

struct test_case {
	int		flag;
	const char	*str;
	const char	**argv;
};

static const struct test_case *tests[] = {
#define TEST_PASS(flag, str, ...)					\
	&(const struct test_case){flag, str,				\
	    (const char **)&(const void *[]){NULL, __VA_ARGS__, NULL}}
#define TEST_FAIL(flag, str, err)					\
	&(const struct test_case){flag, str,				\
	    (const char **)&(const void *[]){err_ ## err, NULL}}
#define K ARGV_COMMENT
#define C ARGV_COMMA
#define N ARGV_NOESC
	TEST_PASS(K|C|N, "", NULL),
	TEST_PASS(0    , "foo", "foo"),
	TEST_PASS(0    , "foo bar", "foo", "bar"),
	TEST_PASS(  C  , "foo bar", "foo", "bar"),
	TEST_PASS(  C  , "foo,bar", "foo", "bar"),
	TEST_PASS(0    , "  foo  bar  ", "foo", "bar"),
	TEST_PASS(  C  , "  foo  ,  bar  ", "foo", "bar"),
	TEST_PASS(  C  , "foo bar ", "foo", "bar"),
	TEST_PASS(  C  , "foo,bar,", "foo", "bar", ""),
	TEST_PASS(0    , "foo \"bar baz\"", "foo", "bar baz"),
	TEST_PASS(0    , "foo #bar", "foo", "#bar"),
	TEST_PASS(K    , "foo #bar", "foo"),
	TEST_PASS(0    , "foo#bar", "foo#bar"),
	TEST_PASS(K    , "foo#bar", "foo#bar"),
	TEST_PASS(    N, "\\", "\\"),
	TEST_FAIL(0    , "\\", invalid_backslash),
	TEST_FAIL(0    , "\\x", invalid_backslash),
	TEST_FAIL(0    , "\\x2", invalid_backslash),
	TEST_FAIL(0    , "\\x2O", invalid_backslash),
	TEST_PASS(0    , "\\x20", " "),
	TEST_FAIL(0    , "\"foo", missing_quote),
	TEST_PASS(    N, "foo\"bar", "foo\"bar"),
	TEST_FAIL(0    , "foo\"bar", invalid_quote),
	TEST_FAIL(0    , "foo\"bar", invalid_quote),
	TEST_PASS(    N, "\"foo\"\"bar\"", "\"foo\"\"bar\""),
	TEST_FAIL(0    , "\"foo\"\"bar\"", missing_separator),
	NULL
#undef N
#undef C
#undef K
#undef TEST_FAIL
#undef TEST_PASS
};

static char **
test_run(const struct test_case *tc, int *ret)
{
	const char *exp, *act;
	char **argv, *tmp;
	int argc, i;

	i = strlen(tc->str);
	if (i == 0) {
		argv = VAV_Parse(tc->str, &argc, tc->flag);
	} else {
		tmp = malloc(i); /* sanitizer-friendly */
		AN(tmp);
		memcpy(tmp, tc->str, i);
		argv = VAV_ParseTxt(tmp, tmp + i, &argc, tc->flag);
		free(tmp);
	}
	AN(argv);

	if (tc->argv[0] != argv[0]) {
		exp = tc->argv[0] != NULL ? tc->argv[0] : "success";
		act = argv[0] != NULL ? argv[0] : "success";
		printf(
		    "ERROR: Parsing string <%s> with flags %x, "
		    "expected <%s> got <%s>.\n",
		    tc->str, tc->flag, exp, act);
		*ret = 1;
		return (argv);
	}

	if (tc->argv[0] != NULL)
		return (argv);

	for (i = 1; i < argc && tc->argv[i] != NULL && argv[i] != NULL; i++) {
		if (!strcmp(tc->argv[i], argv[i]))
			continue;
		printf(
		    "ERROR: Parsing string <%s> with flags %x, "
		    "expected <%s> for argv[%d] got <%s>.\n",
		    tc->str, tc->flag, tc->argv[i], i, argv[i]);
		*ret = 1;
		return (argv);
	}

	if (tc->argv[i] != NULL || argv[i] != NULL) {
		act = i < argc ? "less" : "more";
		printf(
		    "ERROR: Parsing string <%s> with flags %x, "
		    "got %s arguments (%d) than expected.\n",
		    tc->str, tc->flag, act, argc);
		*ret = 1;
		return (argv);
	}

	exp = tc->argv[0] == NULL ? "PASS" : "FAIL";
	printf("%s: <%s> with flags %x as expected.\n", exp, tc->str, tc->flag);
	return (argv);
}

int
main(int argc, char **argv)
{
	const struct test_case **tc;
	int ret = 0;

	(void)argc;
	(void)argv;

	for (tc = tests; ret == 0 && *tc != NULL; tc++) {
		argv = test_run(*tc, &ret);
		VAV_Free(argv);
	}

	return (0);
}
#endif /* TEST_DRIVER */
