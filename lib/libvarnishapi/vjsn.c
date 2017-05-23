/*-
 * Copyright (c) 2017 Varnish Software AS
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
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vas.h"
#include "miniobj.h"
#include "vqueue.h"
#include "vjsn.h"

const char VJSN_OBJECT[] = "object";
const char VJSN_ARRAY[] = "array";
const char VJSN_NUMBER[] = "number";
const char VJSN_STRING[] = "string";
const char VJSN_TRUE[] = "true";
const char VJSN_FALSE[] = "false";
const char VJSN_NULL[] = "null";

#define VJSN_EXPECT(js, xxx, ret)					\
	do {								\
		AZ(js->err);						\
		if (*((js)->ptr) != xxx) {				\
			js->err = "Expected " #xxx " not found.";	\
			return (ret);					\
		} else {						\
			*js->ptr++ = '\0';				\
		}							\
	} while (0)

static struct vjsn_val *vjsn_value(struct vjsn *);

static struct vjsn_val *
vjsn_val_new(const char *type)
{
	struct vjsn_val *jsv;

	ALLOC_OBJ(jsv, VJSN_VAL_MAGIC);
	AN(jsv);
	VTAILQ_INIT(&jsv->children);
	jsv->type = type;
	return (jsv);
}

static void
vjsn_val_delete(struct vjsn_val *jsv)
{
	struct vjsn_val *jsve;

	CHECK_OBJ_NOTNULL(jsv, VJSN_VAL_MAGIC);
	do {
		jsve = VTAILQ_FIRST(&jsv->children);
		if (jsve != NULL) {
			VTAILQ_REMOVE(&jsv->children, jsve, list);
			vjsn_val_delete(jsve);
		}
	} while (jsve != NULL);
	FREE_OBJ(jsv);
}

static void
vjsn_delete(struct vjsn **jp)
{
	struct vjsn *js;

	AN(jp);
	js = *jp;
	*jp = NULL;
	CHECK_OBJ_NOTNULL(js, VJSN_MAGIC);
	if (js->value != NULL)
		vjsn_val_delete(js->value);
	free(js->raw);
	FREE_OBJ(js);
}

static void
vjsn_skip_ws(struct vjsn *js)
{
	char c;

	while (1) {
		c = js->ptr[0];
		if (c == 0x09 || c == 0x0a || c == 0x0d || c == 0x20) {
			*js->ptr++ = '\0';
			continue;
		}
#ifdef VJSN_COMMENTS
		if (c == '/' && js->ptr[1] == '*') {
			js->ptr += 2;
			while (js->ptr[0] != '*' || js->ptr[1] != '/')
				js->ptr++;
			js->ptr += 2;
			continue;
		}
#endif
		return;
	}
}

static unsigned
vjsn_unumber(struct vjsn *js)
{
	unsigned u = 0;
	char c;
	int i;

	VJSN_EXPECT(js, '\\', 0);
	VJSN_EXPECT(js, 'u', 0);
	for(i = 0; i < 4; i++) {
		u <<= 4;
		c = *js->ptr;
		if (c >= '0' && c <= '9')
			u |= c - '0';			/*lint !e737 */
		else if (c >= 'A' && c <= 'F')
			u |= c - '7';			/*lint !e737 */
		else if (c >= 'a' && c <= 'f')
			u |= c - 'W';			/*lint !e737 */
		else {
			js->err = "Illegal \\uXXXX sequence";
			return (0);
		}
		js->ptr++;
	}
	return (u);
}

static void
vjsn_unicode(struct vjsn *js, char **d)
{
	unsigned u1, u2;

	u1 = vjsn_unumber(js);

	if (u1 >= 0xd800 && u1 <= 0xdc00) {
		u2 = vjsn_unumber(js);
		if (u2 < 0xdc00 || u2 > 0xdfff) {
			js->err = "Bad UTF-16 Surrogate pair";
			return;
		}
		u1 -= 0xd800;
		u2 -= 0xdc00;
		u1 <<= 10;
		u1 |= u2;
	}
	/*lint -save -e734 -e713 */
	if (u1 < 0x80)
		*(*d)++ = u1;
	else if (u1 < 0x800) {
		*(*d)++ = 0xc0 + u1 / 64;
		*(*d)++ = 0x80 + u1 % 64;
	} else if (u1 - 0xd800u < 0x800) {
		js->err = "Bad UNICODE point";
	} else if (u1 < 0x10000) {
		*(*d)++ = 0xe0 + u1 / 4096;
		*(*d)++ = 0x80 + u1 / 64 % 64;
		*(*d)++ = 0x80 + u1 % 64;
	} else if (u1 < 0x110000) {
		*(*d)++ = 0xf0 + u1 / 262144;
		*(*d)++ = 0x80 + u1 / 4096 % 64;
		*(*d)++ = 0x80 + u1 / 64 % 64;
		*(*d)++ = 0x80 + u1 % 64;
	} else {
		js->err = "Bad UNICODE point";
	}
	/*lint -restore */
}

static char *
vjsn_string(struct vjsn *js)
{
	char *p, *b;

	vjsn_skip_ws(js);
	VJSN_EXPECT(js, '"', NULL);
	b = p = js->ptr;
	while (*js->ptr != '"') {
		if (*js->ptr == '\0') {
			js->err = "Unterminate string";
			return (NULL);
		}
		if (*js->ptr >= 0x00 && *js->ptr <= 0x1f) {
			js->err = "unescaped control char in string";
			return (NULL);
		}
		if (*js->ptr != '\\') {
			*p++ = *js->ptr++;
			continue;
		}
		switch(js->ptr[1]) {
		case '\\':
		case '/':
		case '"': *p++ = js->ptr[1]; js->ptr += 2; break;
		case 'b': *p++ = 0x08; js->ptr += 2; break;
		case 'f': *p++ = 0x0c; js->ptr += 2; break;
		case 't': *p++ = 0x09; js->ptr += 2; break;
		case 'n': *p++ = 0x0a; js->ptr += 2; break;
		case 'r': *p++ = 0x0d; js->ptr += 2; break;
		case 'u':
			vjsn_unicode(js, &p);
			if(js->err != NULL)
				return(NULL);
			break;
		default:
			js->err = "Bad string escape";
			return (NULL);
		}
	}
	VJSN_EXPECT(js, '"', NULL);
	*p = '\0';
	return (b);
}

static struct vjsn_val *
vjsn_object(struct vjsn *js)
{
	struct vjsn_val *jsv, *jsve;
	char *s;

	VJSN_EXPECT(js, '{', NULL);

	jsv = vjsn_val_new(VJSN_OBJECT);
	AN(jsv);

	vjsn_skip_ws(js);
	if (*js->ptr != '}') {
		while (1) {
			s = vjsn_string(js);
			if (js->err != NULL)
				return (jsv);
			vjsn_skip_ws(js);
			VJSN_EXPECT(js, ':', jsv);
			jsve = vjsn_value(js);
			if (js->err != NULL)
				return (jsv);
			CHECK_OBJ_NOTNULL(jsve, VJSN_VAL_MAGIC);
			jsve->name = s;
			VTAILQ_INSERT_TAIL(&jsv->children, jsve, list);
			vjsn_skip_ws(js);
			if(*js->ptr == '}')
				break;
			VJSN_EXPECT(js, ',', jsv);
		}
	}
	VJSN_EXPECT(js, '}', jsv);
	return (jsv);
}

static struct vjsn_val *
vjsn_array(struct vjsn *js)
{
	struct vjsn_val *jsv, *jsve;

	VJSN_EXPECT(js, '[', NULL);

	jsv = vjsn_val_new(VJSN_ARRAY);
	AN(jsv);

	vjsn_skip_ws(js);
	if (*js->ptr != ']') {
		while (1) {
			jsve = vjsn_value(js);
			if (js->err != NULL)
				return (jsv);
			CHECK_OBJ_NOTNULL(jsve, VJSN_VAL_MAGIC);
			VTAILQ_INSERT_TAIL(&jsv->children, jsve, list);
			vjsn_skip_ws(js);
			if(*js->ptr == ']')
				break;
			VJSN_EXPECT(js, ',', jsv);
		}
	}
	VJSN_EXPECT(js, ']', jsv);
	return (jsv);
}

static struct vjsn_val *
vjsn_number(struct vjsn *js)
{
	struct vjsn_val *jsv;

	jsv = vjsn_val_new(VJSN_NUMBER);
	AN(jsv);

	jsv->value = js->ptr;

	if (*js->ptr == '-')
		js->ptr++;
	if (*js->ptr < '0' || *js->ptr > '9') {
		js->err = "Bad number";
		return (jsv);
	}
	if (*js->ptr == '0' && js->ptr[1] >= '0' && js->ptr[1] <= '9') {
		js->err = "Bad number";
		return (jsv);
	}
	while (*js->ptr >= '0' && *js->ptr <= '9')
		js->ptr++;
	if (*js->ptr == '.') {
		js->ptr++;
		if (*js->ptr < '0' || *js->ptr > '9') {
			js->err = "Bad number";
			return (jsv);
		}
		while (*js->ptr >= '0' && *js->ptr <= '9')
			js->ptr++;
	}
	if (*js->ptr == 'e' || *js->ptr == 'E') {
		js->ptr++;
		if (*js->ptr == '-' || *js->ptr == '+')
			js->ptr++;
		if (*js->ptr < '0' || *js->ptr > '9') {
			js->err = "Bad number";
			return (jsv);
		}
		while (*js->ptr >= '0' && *js->ptr <= '9')
			js->ptr++;
	}
	return (jsv);
}

static struct vjsn_val *
vjsn_value(struct vjsn *js)
{
	struct vjsn_val *jsv;

	AZ(js->err);
	vjsn_skip_ws(js);
	if (*js->ptr == '{')
		return (vjsn_object(js));
	if (*js->ptr== '[')
		return (vjsn_array(js));
	if (*js->ptr== '"') {
		jsv = vjsn_val_new(VJSN_STRING);
		jsv->value = vjsn_string(js);
		if (js->err != NULL)
			return (jsv);
		AN(jsv->value);
		return (jsv);
	}
	if (!memcmp(js->ptr, "true", 4)) {
		js->ptr += 4;
		return (vjsn_val_new(VJSN_TRUE));
	}
	if (!memcmp(js->ptr, "false", 4)) {
		js->ptr += 5;
		return (vjsn_val_new(VJSN_FALSE));
	}
	if (!memcmp(js->ptr, "null", 4)) {
		js->ptr += 4;
		return (vjsn_val_new(VJSN_NULL));
	}
	if (*js->ptr == '-' || (*js->ptr >= '0' && *js->ptr <= '9'))
		return (vjsn_number(js));
	js->err = "Unrecognized value";
	return (NULL);
}

struct vjsn *
vjsn_parse(const char *src, const char **err)
{
	struct vjsn *js;
	char *p, *e;

	AN(src);

	AN(err);
	*err = NULL;

	p = strdup(src);
	AN(p);
	e = strchr(p, '\0');
	AN(e);

	ALLOC_OBJ(js, VJSN_MAGIC);
	AN(js);
	js->raw = p;
	js->ptr = p;

	js->value = vjsn_value(js);
	if (js->err != NULL) {
		*err = js->err;
		vjsn_delete(&js);
		return (NULL);
	}

	vjsn_skip_ws(js);
	if (js->ptr != e) {
		*err = "Garbage after value";
		vjsn_delete(&js);
		return (NULL);
	}
	return (js);
}

static void
vjsn_dump_i(const struct vjsn_val *jsv, FILE *fo, int indent)
{
	struct vjsn_val *jsve;

	printf("%*s", indent, "");
	if (jsv->name != NULL)
		printf("[\"%s\"]: ", jsv->name);
	printf("{%s}", jsv->type);
	if (jsv->value != NULL) {
		if (strlen(jsv->value) < 20)
			printf(" <%s", jsv->value);
		else
			printf(" <%.10s[...#%zu]",
			    jsv->value, strlen(jsv->value) - 10L);
		printf(">");
	}
	printf("\n");
	VTAILQ_FOREACH(jsve, &jsv->children, list)
		vjsn_dump_i(jsve, fo, indent + 2);
}

void
vjsn_dump(const struct vjsn *js, FILE *fo)
{

	CHECK_OBJ_NOTNULL(js, VJSN_MAGIC);
	AN(fo);
	vjsn_dump_i(js->value, fo, 0);
}

#ifdef VJSN_TEST

static struct vjsn *
vjsn_file(const char *fn, const char **err)
{
	int fd;
	struct stat st;
	char *p, *e;
	ssize_t sz;
	struct vjsn *js;

	AN(fn);
	AN(err);
	*err = NULL;
	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		*err = strerror(errno);
		return (NULL);
	}
	AZ(fstat(fd, &st));
	if (!S_ISREG(st.st_mode)) {
		AZ(close(fd));
		*err = "Not a regular file";
		return (NULL);
	}

	if (st.st_size == 0) {
		AZ(close(fd));
		*err = "Empty file";
		return (NULL);
	}

	p = malloc(st.st_size + 1);
	AN(p);

	sz = read(fd, p, st.st_size + 1);
	if (sz < 0) {
		*err = strerror(errno);
		AZ(close(fd));
		free(p);
		return (NULL);
	}
	AZ(close(fd));

	if (sz < st.st_size) {
		free(p);
		*err = "Short read";
		return (NULL);
	}

	if (sz > st.st_size) {
		free(p);
		*err = "Long read";
		return (NULL);
	}

	p[sz] = '\0';
	e = p + sz;

	ALLOC_OBJ(js, VJSN_MAGIC);
	AN(js);
	js->raw = p;
	js->ptr = p;

	js->value = vjsn_value(js);
	if (js->err != NULL) {
		*err = js->err;
		vjsn_delete(&js);
		return (NULL);
	}

	vjsn_skip_ws(js);
	if (js->ptr != e) {
		*err = "Garbage after value";
		vjsn_delete(&js);
		return (NULL);
	}
	return (js);
}


static const char * const fnx = "tst.vjsn";

int
main(int argc, char **argv)
{
	struct vjsn *js;
	const char *err;

	if (argc == 1)
		js = vjsn_file(fnx, &err);
	else
		js = vjsn_file(argv[1], &err);
	if (err != NULL) {
		fprintf(stderr, "ERROR: %s\n", err);
		AZ(js);
		return (1);
	} else {
		if (0)
			vjsn_dump(js, stdout);
		vjsn_delete(&js);
	}
	return (0);
}

#endif
