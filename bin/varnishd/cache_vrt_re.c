/*
 * $Id$
 *
 * Runtime support for compiled VCL programs, regexps
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <regex.h>

#include "shmlog.h"
#include "vrt.h"
#include "vrt_obj.h"
#include "sbuf.h"
#include "vcl.h"
#include "cache.h"

void
VRT_re_init(void **rep, const char *re)
{
	regex_t	*t;
	int i;

	t = calloc(sizeof *t, 1);
	assert(t != NULL);
	i = regcomp(t, re, REG_EXTENDED | REG_NOSUB);
	assert(i == 0);
	*rep = t;
}

void
VRT_re_fini(void *rep)
{

	if (rep != NULL)
		regfree(rep);
}

int
VRT_re_match(const char *s, void *re)
{
	regex_t	*t;
	int i;

	t = re;
	i = regexec(t, s, 0, NULL, 0);
	if (i == 0)
		return (1);
	assert(i == REG_NOMATCH);
	return (0);
}

int
VRT_re_test(struct sbuf *sb, const char *re)
{
	int i, j;
	regex_t	t;	
	char buf[BUFSIZ];

	memset(&t, 0, sizeof t);
	i = regcomp(&t, re, REG_EXTENDED | REG_NOSUB);
	if (i == 0) {
		regfree(&t);
		return (0);
	}
	j = regerror(i, &t, buf, sizeof buf);
	sbuf_printf(sb, "Regexp compilation error:\n\n%s\n\n", buf);
	regfree(&t);
	return (1);
}
