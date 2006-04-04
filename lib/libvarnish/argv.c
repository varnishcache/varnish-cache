/*
 * $Id$
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

#include <ctype.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "libvarnish.h"

static int
BackSlash(const char *s, int *res)
{
	int i, r;
	unsigned u;

	assert(*s == '\\');
	r = i = 0;
	switch(s[1]) {
	case 'n':
		i = '\n';
		r = 2;
		break;
	case 'r':
		i = '\r';
		r = 2;
		break;
	case 't':
		i = '\t';
		r = 2;
		break;
	case '"':
		i = '"';
		r = 2;
		break;
	case '\\':
		i = '\\';
		r = 2;
		break;
	case '0': case '1': case '2': case '3':
	case '4': case '5': case '6': case '7':
		for (r = 1; r < 4; r++) {
			if (!isdigit(s[r]))
				break;
			if (s[r] - '0' > 7)
				break;
			i <<= 3;
			i |= s[r] - '0';
		}
		break;
	case 'x':
		if (1 == sscanf(s + 1, "x%02x", &u)) {
			i = u;
			r = 4;
		}
		break;
	default:
		break;
	}
	if (res != NULL)
		*res = i;
	return (r);
}

static char *
BackSlashDecode(const char *s, const char *e)
{
	const char *q;
	char *p, *r;
	int i, j;

	p = calloc((e - s) + 1, 1);
	if (p == NULL)
		return (p);
	for (r = p, q = s; q < e; ) {
		if (*q != '\\') {
			*r++ = *q++;
			continue;
		}
		i = BackSlash(q, &j);
		q += i;
		*r++ = j;
	}
	*r = '\0';
	return (p);
}

const char **
ParseArgv(const char *s, int comment)
{
	const char **argv;
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
		if (comment && *s == '#')
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
					argv[0] = "Illegal backslash sequence";
					return (argv);
				}
				s += i;
				continue;
			}
			if (!quote) {
				if (*s == '\0' || isspace(*s))
					break;
				s++;
				continue;
			}
			if (*s == '"')
				break;
			if (*s == '\0') {
				argv[0] = "Missing '\"'";
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
FreeArgv(const char **argv)
{
	int i;
	
	for (i = 1; argv[i] != NULL; i++)
		free((void *)(uintptr_t)argv[i]);
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
main(int argc __unused, char **argv __unused)
{
	char buf[BUFSIZ];

        register_printf_render_std("V");

	while (fgets(buf, sizeof buf, stdin))
		Test(buf);

	return (0);
}
#endif
