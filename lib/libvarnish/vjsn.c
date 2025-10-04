/*-
 * Copyright (c) 2017 Varnish Software AS
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
 */

#include "config.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "vdef.h"

#include "vas.h"
#include "miniobj.h"
#include "vqueue.h"
#include "vjsn.h"

#define VJSN_TYPE_MACRO(UPPER, lower) \
	static const char VJSN_##UPPER[] = #lower;
VJSN_TYPES
#undef VJSN_TYPE_MACRO

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

void
vjsn_delete(struct vjsn **jp)
{
	struct vjsn *js;

	TAKE_OBJ_NOTNULL(js, jp, VJSN_MAGIC);
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
	for (i = 0; i < 4; i++) {
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
	if (js->err)
		return;

	if (u1 >= 0xdc00 && u1 <= 0xdfff) {
		js->err = "Lone second UTF-16 Surrogate";
		return;
	}
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
		u1 |= 0x10000;
	}
	assert(u1 < 0x110000);
	/*lint -save -e734 -e713 */
	if (u1 < 0x80)
		*(*d)++ = u1;
	else if (u1 < 0x800) {
		*(*d)++ = 0xc0 + u1 / 64;
		*(*d)++ = 0x80 + u1 % 64;
	} else if (u1 < 0x10000) {
		*(*d)++ = 0xe0 + u1 / 4096;
		*(*d)++ = 0x80 + u1 / 64 % 64;
		*(*d)++ = 0x80 + u1 % 64;
	} else {
		*(*d)++ = 0xf0 + u1 / 262144;
		*(*d)++ = 0x80 + u1 / 4096 % 64;
		*(*d)++ = 0x80 + u1 / 64 % 64;
		*(*d)++ = 0x80 + u1 % 64;
	}
	/*lint -restore */
}

static char *
vjsn_string(struct vjsn *js, char **e)
{
	char *p, *b;

	AN(e);
	vjsn_skip_ws(js);
	VJSN_EXPECT(js, '"', NULL);
	b = p = js->ptr;
	while (*js->ptr != '"') {
		if (*js->ptr == '\0') {
			js->err = "Unterminated string";
			return (NULL);
		}
		if ((unsigned char)(*js->ptr) <= 0x1f) {
			js->err = "Unescaped control char in string";
			return (NULL);
		}
		if (*js->ptr != '\\') {
			*p++ = *js->ptr++;
			continue;
		}
		switch (js->ptr[1]) {
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
			if (js->err != NULL)
				return (NULL);
			break;
		default:
			js->err = "Bad string escape";
			return (NULL);
		}
	}
	VJSN_EXPECT(js, '"', NULL);
	*p = '\0';
	*e = p;
	return (b);
}

static struct vjsn_val *
vjsn_object(struct vjsn *js)
{
	struct vjsn_val *jsv, *jsve;
	char *s, *e;

	VJSN_EXPECT(js, '{', NULL);

	jsv = vjsn_val_new(VJSN_OBJECT);
	AN(jsv);

	vjsn_skip_ws(js);
	if (*js->ptr != '}') {
		while (1) {
			s = vjsn_string(js, &e);
			if (js->err != NULL)
				return (jsv);
			vjsn_skip_ws(js);
			VJSN_EXPECT(js, ':', jsv);
			jsve = vjsn_value(js);
			if (js->err != NULL) {
				if (jsve != NULL)
					vjsn_val_delete(jsve);
				return (jsv);
			}
			CHECK_OBJ_NOTNULL(jsve, VJSN_VAL_MAGIC);
			jsve->name = s;
			jsve->name_e = e;
			VTAILQ_INSERT_TAIL(&jsv->children, jsve, list);
			vjsn_skip_ws(js);
			if (*js->ptr == '}')
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
			if (js->err != NULL) {
				if (jsve != NULL)
					vjsn_val_delete(jsve);
				return (jsv);
			}
			CHECK_OBJ_NOTNULL(jsve, VJSN_VAL_MAGIC);
			VTAILQ_INSERT_TAIL(&jsv->children, jsve, list);
			vjsn_skip_ws(js);
			if (*js->ptr == ']')
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
	/*
	 * The terminating NUL is supplied by the caller, once they
	 * have decided if they like what occupies that spot.
	 */
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
		jsv->value = vjsn_string(js, &jsv->value_e);
		if (js->err != NULL)
			return (jsv);
		AN(jsv->value);
		return (jsv);
	}
	if (!strncmp(js->ptr, "true", 4)) {
		js->ptr += 4;
		return (vjsn_val_new(VJSN_TRUE));
	}
	if (!strncmp(js->ptr, "false", 5)) {
		js->ptr += 5;
		return (vjsn_val_new(VJSN_FALSE));
	}
	if (!strncmp(js->ptr, "null", 4)) {
		js->ptr += 4;
		return (vjsn_val_new(VJSN_NULL));
	}
	if (*js->ptr == '-' || (*js->ptr >= '0' && *js->ptr <= '9'))
		return (vjsn_number(js));
	js->err = "Unrecognized value";
	return (NULL);
}

struct vjsn *
vjsn_parse_end(const char *from, const char *to, const char **err)
{
	struct vjsn *js;
	char *p, *e;
	size_t sz;

	AN(from);

	AN(err);
	*err = NULL;

	if (to == NULL)
		to = strchr(from, '\0');
	AN(to);

	sz = to - from;

	p = malloc(sz + 1L);
	AN(p);
	memcpy(p, from, sz);
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

struct vjsn *
vjsn_parse(const char *src, const char **err)
{

	return (vjsn_parse_end(src, NULL, err));
}

struct vjsn_val *
vjsn_child(const struct vjsn_val *vv, const char *key)
{
	struct vjsn_val *vc;

	CHECK_OBJ_NOTNULL(vv, VJSN_VAL_MAGIC);
	AN(key);
	VTAILQ_FOREACH(vc, &vv->children, list) {
		if (vc->name != NULL && !strcmp(vc->name, key))
			return (vc);
	}
	return (NULL);
}

static void
vjsn_dump_i(const struct vjsn_val *jsv, FILE *fo, int indent)
{
	struct vjsn_val *jsve;

	CHECK_OBJ_NOTNULL(jsv, VJSN_VAL_MAGIC);
	printf("%*s", indent, "");
	if (jsv->name != NULL)
		printf("[\"%s\"]: ", jsv->name);
	printf("{%s}", jsv->type);
	if (jsv->value != NULL) {
		if (strlen(jsv->value) < 20)
			printf(" <%s", jsv->value);
		else
			printf(" <%.10s[...#%zu]",
			    jsv->value, strlen(jsv->value + 10));
		printf(">");
	}
	printf("\n");
	VTAILQ_FOREACH(jsve, &jsv->children, list)
		vjsn_dump_i(jsve, fo, indent + 2);
}

void
vjsn_dump_val(const struct vjsn_val *jsv, FILE *fo)
{
	CHECK_OBJ_NOTNULL(jsv, VJSN_VAL_MAGIC);
	vjsn_dump_i(jsv, fo, 0);
}

void
vjsn_dump(const struct vjsn *js, FILE *fo)
{

	CHECK_OBJ_NOTNULL(js, VJSN_MAGIC);
	AN(fo);
	vjsn_dump_i(js->value, fo, 0);
}

#define VJSN_TYPE_MACRO(UPPER, lower) \
	int \
	vjsn_is_##lower(const struct vjsn_val *jsv) \
	{ \
		CHECK_OBJ_NOTNULL(jsv, VJSN_VAL_MAGIC); \
		return (jsv->type == VJSN_##UPPER); \
	}
VJSN_TYPES
#undef VJSN_TYPE_MACRO


#ifdef VJSN_TEST

/*
 * Test-cases by Nicolas Seriot
 *
 * See: http://seriot.ch/parsing_json.php
 *
 * MIT License
 *
 * Copyright (c) 2016 Nicolas Seriot
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * We skip tests containing NUL, because we're lazy (The code will actually
 * pass these tests if you provide for a way to pass the true length of the
 * input to it, and we skip really huge tests, because we are only limited
 * by available memory.
 *
 * To produce the C-data-structures:
 *
 * Clone https://github.com/nst/JSONTestSuite.git
 *
 * And run this python in test_parsing:

import glob

def emit(fin):
    x = bytearray(open(fin, 'rb').read())
    if 0 in x:
        print("\t// SKIP: NUL in", fin)
        return
    if len(x) > 1000:
        print("\t// SKIP: LONG ", fin)
        return
    t = '\t"'
    for i in x:
        t += "\\x%02x" % i
        if len(t) > 64:
            print(t + '"')
            t = '\t"'
    print(t + '",')

print("static const char *good[] = {")
for f in sorted(glob.glob("y_*")):
    emit(f)
print("\tNULL")
print("};\n")

print("static const char *bad[] = {")
for f in sorted(glob.glob("n_*")):
        emit(f)
print("\tNULL")
print("};")

 */

static const char *good[] = {
	"\x5b\x5b\x5d\x20\x20\x20\x5d",
	"\x5b\x22\x22\x5d",
	"\x5b\x5d",
	"\x5b\x22\x61\x22\x5d",
	"\x5b\x66\x61\x6c\x73\x65\x5d",
	"\x5b\x6e\x75\x6c\x6c\x2c\x20\x31\x2c\x20\x22\x31\x22\x2c\x20\x7b"
	"\x7d\x5d",
	"\x5b\x6e\x75\x6c\x6c\x5d",
	"\x5b\x31\x0a\x5d",
	"\x20\x5b\x31\x5d",
	"\x5b\x31\x2c\x6e\x75\x6c\x6c\x2c\x6e\x75\x6c\x6c\x2c\x6e\x75\x6c"
	"\x6c\x2c\x32\x5d",
	"\x5b\x32\x5d\x20",
	"\x5b\x31\x32\x33\x65\x36\x35\x5d",
	"\x5b\x30\x65\x2b\x31\x5d",
	"\x5b\x30\x65\x31\x5d",
	"\x5b\x20\x34\x5d",
	"\x5b\x2d\x30\x2e\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30"
	"\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30"
	"\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30"
	"\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30"
	"\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30"
	"\x30\x31\x5d\x0a",
	"\x5b\x32\x30\x65\x31\x5d",
	"\x5b\x2d\x30\x5d",
	"\x5b\x2d\x31\x32\x33\x5d",
	"\x5b\x2d\x31\x5d",
	"\x5b\x2d\x30\x5d",
	"\x5b\x31\x45\x32\x32\x5d",
	"\x5b\x31\x45\x2d\x32\x5d",
	"\x5b\x31\x45\x2b\x32\x5d",
	"\x5b\x31\x32\x33\x65\x34\x35\x5d",
	"\x5b\x31\x32\x33\x2e\x34\x35\x36\x65\x37\x38\x5d",
	"\x5b\x31\x65\x2d\x32\x5d",
	"\x5b\x31\x65\x2b\x32\x5d",
	"\x5b\x31\x32\x33\x5d",
	"\x5b\x31\x32\x33\x2e\x34\x35\x36\x37\x38\x39\x5d",
	"\x7b\x22\x61\x73\x64\x22\x3a\x22\x73\x64\x66\x22\x2c\x20\x22\x64"
	"\x66\x67\x22\x3a\x22\x66\x67\x68\x22\x7d",
	"\x7b\x22\x61\x73\x64\x22\x3a\x22\x73\x64\x66\x22\x7d",
	"\x7b\x22\x61\x22\x3a\x22\x62\x22\x2c\x22\x61\x22\x3a\x22\x63\x22"
	"\x7d",
	"\x7b\x22\x61\x22\x3a\x22\x62\x22\x2c\x22\x61\x22\x3a\x22\x62\x22"
	"\x7d",
	"\x7b\x7d",
	"\x7b\x22\x22\x3a\x30\x7d",
	"\x7b\x22\x66\x6f\x6f\x5c\x75\x30\x30\x30\x30\x62\x61\x72\x22\x3a"
	"\x20\x34\x32\x7d",
	"\x7b\x20\x22\x6d\x69\x6e\x22\x3a\x20\x2d\x31\x2e\x30\x65\x2b\x32"
	"\x38\x2c\x20\x22\x6d\x61\x78\x22\x3a\x20\x31\x2e\x30\x65\x2b\x32"
	"\x38\x20\x7d",
	"\x7b\x22\x78\x22\x3a\x5b\x7b\x22\x69\x64\x22\x3a\x20\x22\x78\x78"
	"\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78"
	"\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78"
	"\x78\x78\x78\x78\x78\x78\x22\x7d\x5d\x2c\x20\x22\x69\x64\x22\x3a"
	"\x20\x22\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78"
	"\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78"
	"\x78\x78\x78\x78\x78\x78\x78\x78\x78\x78\x22\x7d",
	"\x7b\x22\x61\x22\x3a\x5b\x5d\x7d",
	"\x7b\x22\x74\x69\x74\x6c\x65\x22\x3a\x22\x5c\x75\x30\x34\x31\x66"
	"\x5c\x75\x30\x34\x33\x65\x5c\x75\x30\x34\x33\x62\x5c\x75\x30\x34"
	"\x34\x32\x5c\x75\x30\x34\x33\x65\x5c\x75\x30\x34\x34\x30\x5c\x75"
	"\x30\x34\x33\x30\x20\x5c\x75\x30\x34\x31\x37\x5c\x75\x30\x34\x33"
	"\x35\x5c\x75\x30\x34\x33\x63\x5c\x75\x30\x34\x33\x62\x5c\x75\x30"
	"\x34\x33\x35\x5c\x75\x30\x34\x33\x61\x5c\x75\x30\x34\x33\x65\x5c"
	"\x75\x30\x34\x33\x66\x5c\x75\x30\x34\x33\x30\x22\x20\x7d",
	"\x7b\x0a\x22\x61\x22\x3a\x20\x22\x62\x22\x0a\x7d",
	"\x5b\x22\x5c\x75\x30\x30\x36\x30\x5c\x75\x30\x31\x32\x61\x5c\x75"
	"\x31\x32\x41\x42\x22\x5d",
	"\x5b\x22\x5c\x75\x44\x38\x30\x31\x5c\x75\x64\x63\x33\x37\x22\x5d"
	"",
	"\x5b\x22\x5c\x75\x64\x38\x33\x64\x5c\x75\x64\x65\x33\x39\x5c\x75"
	"\x64\x38\x33\x64\x5c\x75\x64\x63\x38\x64\x22\x5d",
	"\x5b\x22\x5c\x22\x5c\x5c\x5c\x2f\x5c\x62\x5c\x66\x5c\x6e\x5c\x72"
	"\x5c\x74\x22\x5d",
	"\x5b\x22\x5c\x5c\x75\x30\x30\x30\x30\x22\x5d",
	"\x5b\x22\x5c\x22\x22\x5d",
	"\x5b\x22\x61\x2f\x2a\x62\x2a\x2f\x63\x2f\x2a\x64\x2f\x2f\x65\x22"
	"\x5d",
	"\x5b\x22\x5c\x5c\x61\x22\x5d",
	"\x5b\x22\x5c\x5c\x6e\x22\x5d",
	"\x5b\x22\x5c\x75\x30\x30\x31\x32\x22\x5d",
	"\x5b\x22\x5c\x75\x46\x46\x46\x46\x22\x5d",
	"\x5b\x22\x61\x73\x64\x22\x5d",
	"\x5b\x20\x22\x61\x73\x64\x22\x5d",
	"\x5b\x22\x5c\x75\x44\x42\x46\x46\x5c\x75\x44\x46\x46\x46\x22\x5d"
	"",
	"\x5b\x22\x6e\x65\x77\x5c\x75\x30\x30\x41\x30\x6c\x69\x6e\x65\x22"
	"\x5d",
	"\x5b\x22\xf4\x8f\xbf\xbf\x22\x5d",
	"\x5b\x22\xef\xbf\xbf\x22\x5d",
	"\x5b\x22\x5c\x75\x30\x30\x30\x30\x22\x5d",
	"\x5b\x22\x5c\x75\x30\x30\x32\x63\x22\x5d",
	"\x5b\x22\xcf\x80\x22\x5d",
	"\x5b\x22\xf0\x9b\xbf\xbf\x22\x5d",
	"\x5b\x22\x61\x73\x64\x20\x22\x5d",
	"\x22\x20\x22",
	"\x5b\x22\x5c\x75\x44\x38\x33\x34\x5c\x75\x44\x64\x31\x65\x22\x5d"
	"",
	"\x5b\x22\x5c\x75\x30\x38\x32\x31\x22\x5d",
	"\x5b\x22\x5c\x75\x30\x31\x32\x33\x22\x5d",
	"\x5b\x22\xe2\x80\xa8\x22\x5d",
	"\x5b\x22\xe2\x80\xa9\x22\x5d",
	"\x5b\x22\x5c\x75\x30\x30\x36\x31\x5c\x75\x33\x30\x61\x66\x5c\x75"
	"\x33\x30\x45\x41\x5c\x75\x33\x30\x62\x39\x22\x5d",
	"\x5b\x22\x6e\x65\x77\x5c\x75\x30\x30\x30\x41\x6c\x69\x6e\x65\x22"
	"\x5d",
	"\x5b\x22\x7f\x22\x5d",
	"\x5b\x22\x5c\x75\x41\x36\x36\x44\x22\x5d",
	"\x5b\x22\x5c\x75\x30\x30\x35\x43\x22\x5d",
	"\x5b\x22\xe2\x8d\x82\xe3\x88\xb4\xe2\x8d\x82\x22\x5d",
	"\x5b\x22\x5c\x75\x44\x42\x46\x46\x5c\x75\x44\x46\x46\x45\x22\x5d"
	"",
	"\x5b\x22\x5c\x75\x44\x38\x33\x46\x5c\x75\x44\x46\x46\x45\x22\x5d"
	"",
	"\x5b\x22\x5c\x75\x32\x30\x30\x42\x22\x5d",
	"\x5b\x22\x5c\x75\x32\x30\x36\x34\x22\x5d",
	"\x5b\x22\x5c\x75\x46\x44\x44\x30\x22\x5d",
	"\x5b\x22\x5c\x75\x46\x46\x46\x45\x22\x5d",
	"\x5b\x22\x5c\x75\x30\x30\x32\x32\x22\x5d",
	"\x5b\x22\xe2\x82\xac\xf0\x9d\x84\x9e\x22\x5d",
	"\x5b\x22\x61\x7f\x61\x22\x5d",
	"\x66\x61\x6c\x73\x65",
	"\x34\x32",
	"\x2d\x30\x2e\x31",
	"\x6e\x75\x6c\x6c",
	"\x22\x61\x73\x64\x22",
	"\x74\x72\x75\x65",
	"\x22\x22",
	"\x5b\x22\x61\x22\x5d\x0a",
	"\x5b\x74\x72\x75\x65\x5d",
	"\x20\x5b\x5d\x20",
	NULL
};

static const char *bad[] = {
	"\x5b\x31\x20\x74\x72\x75\x65\x5d",
	"\x5b\x61\xe5\x5d",
	"\x5b\x22\x22\x3a\x20\x31\x5d",
	"\x5b\x22\x22\x5d\x2c",
	"\x5b\x2c\x31\x5d",
	"\x5b\x31\x2c\x2c\x32\x5d",
	"\x5b\x22\x78\x22\x2c\x2c\x5d",
	"\x5b\x22\x78\x22\x5d\x5d",
	"\x5b\x22\x22\x2c\x5d",
	"\x5b\x22\x78\x22",
	"\x5b\x78",
	"\x5b\x33\x5b\x34\x5d\x5d",
	"\x5b\xff\x5d",
	"\x5b\x31\x3a\x32\x5d",
	"\x5b\x2c\x5d",
	"\x5b\x2d\x5d",
	"\x5b\x20\x20\x20\x2c\x20\x22\x22\x5d",
	"\x5b\x22\x61\x22\x2c\x0a\x34\x0a\x2c\x31\x2c",
	"\x5b\x31\x2c\x5d",
	"\x5b\x31\x2c\x2c\x5d",
	"\x5b\x22\x0b\x61\x22\x5c\x66\x5d",
	"\x5b\x2a\x5d",
	"\x5b\x22\x22",
	"\x5b\x31\x2c",
	"\x5b\x31\x2c\x0a\x31\x0a\x2c\x31",
	"\x5b\x7b\x7d",
	"\x5b\x66\x61\x6c\x73\x5d",
	"\x5b\x6e\x75\x6c\x5d",
	"\x5b\x74\x72\x75\x5d",
	// SKIP: NUL in n_multidigit_number_then_00.json
	"\x5b\x2b\x2b\x31\x32\x33\x34\x5d",
	"\x5b\x2b\x31\x5d",
	"\x5b\x2b\x49\x6e\x66\x5d",
	"\x5b\x2d\x30\x31\x5d",
	"\x5b\x2d\x31\x2e\x30\x2e\x5d",
	"\x5b\x2d\x32\x2e\x5d",
	"\x5b\x2d\x4e\x61\x4e\x5d",
	"\x5b\x2e\x2d\x31\x5d",
	"\x5b\x2e\x32\x65\x2d\x33\x5d",
	"\x5b\x30\x2e\x31\x2e\x32\x5d",
	"\x5b\x30\x2e\x33\x65\x2b\x5d",
	"\x5b\x30\x2e\x33\x65\x5d",
	"\x5b\x30\x2e\x65\x31\x5d",
	"\x5b\x30\x45\x2b\x5d",
	"\x5b\x30\x45\x5d",
	"\x5b\x30\x65\x2b\x5d",
	"\x5b\x30\x65\x5d",
	"\x5b\x31\x2e\x30\x65\x2b\x5d",
	"\x5b\x31\x2e\x30\x65\x2d\x5d",
	"\x5b\x31\x2e\x30\x65\x5d",
	"\x5b\x31\x20\x30\x30\x30\x2e\x30\x5d",
	"\x5b\x31\x65\x45\x32\x5d",
	"\x5b\x32\x2e\x65\x2b\x33\x5d",
	"\x5b\x32\x2e\x65\x2d\x33\x5d",
	"\x5b\x32\x2e\x65\x33\x5d",
	"\x5b\x39\x2e\x65\x2b\x5d",
	"\x5b\x49\x6e\x66\x5d",
	"\x5b\x4e\x61\x4e\x5d",
	"\x5b\xef\xbc\x91\x5d",
	"\x5b\x31\x2b\x32\x5d",
	"\x5b\x30\x78\x31\x5d",
	"\x5b\x30\x78\x34\x32\x5d",
	"\x5b\x49\x6e\x66\x69\x6e\x69\x74\x79\x5d",
	"\x5b\x30\x65\x2b\x2d\x31\x5d",
	"\x5b\x2d\x31\x32\x33\x2e\x31\x32\x33\x66\x6f\x6f\x5d",
	"\x5b\x31\x32\x33\xe5\x5d",
	"\x5b\x31\x65\x31\xe5\x5d",
	"\x5b\x30\xe5\x5d\x0a",
	"\x5b\x2d\x49\x6e\x66\x69\x6e\x69\x74\x79\x5d",
	"\x5b\x2d\x66\x6f\x6f\x5d",
	"\x5b\x2d\x20\x31\x5d",
	"\x5b\x2d\x30\x31\x32\x5d",
	"\x5b\x2d\x2e\x31\x32\x33\x5d",
	"\x5b\x2d\x31\x78\x5d",
	"\x5b\x31\x65\x61\x5d",
	"\x5b\x31\x65\xe5\x5d",
	"\x5b\x31\x2e\x5d",
	"\x5b\x2e\x31\x32\x33\x5d",
	"\x5b\x31\x2e\x32\x61\x2d\x33\x5d",
	"\x5b\x31\x2e\x38\x30\x31\x31\x36\x37\x30\x30\x33\x33\x33\x37\x36"
	"\x35\x31\x34\x48\x2d\x33\x30\x38\x5d",
	"\x5b\x30\x31\x32\x5d",
	"\x5b\x22\x78\x22\x2c\x20\x74\x72\x75\x74\x68\x5d",
	"\x7b\x5b\x3a\x20\x22\x78\x22\x7d\x0a",
	"\x7b\x22\x78\x22\x2c\x20\x6e\x75\x6c\x6c\x7d",
	"\x7b\x22\x78\x22\x3a\x3a\x22\x62\x22\x7d",
	"\x7b\xf0\x9f\x87\xa8\xf0\x9f\x87\xad\x7d",
	"\x7b\x22\x61\x22\x3a\x22\x61\x22\x20\x31\x32\x33\x7d",
	"\x7b\x6b\x65\x79\x3a\x20\x27\x76\x61\x6c\x75\x65\x27\x7d",
	"\x7b\x22\xb9\x22\x3a\x22\x30\x22\x2c\x7d",
	"\x7b\x22\x61\x22\x20\x62\x7d",
	"\x7b\x3a\x22\x62\x22\x7d",
	"\x7b\x22\x61\x22\x20\x22\x62\x22\x7d",
	"\x7b\x22\x61\x22\x3a",
	"\x7b\x22\x61\x22",
	"\x7b\x31\x3a\x31\x7d",
	"\x7b\x39\x39\x39\x39\x45\x39\x39\x39\x39\x3a\x31\x7d",
	"\x7b\x6e\x75\x6c\x6c\x3a\x6e\x75\x6c\x6c\x2c\x6e\x75\x6c\x6c\x3a"
	"\x6e\x75\x6c\x6c\x7d",
	"\x7b\x22\x69\x64\x22\x3a\x30\x2c\x2c\x2c\x2c\x2c\x7d",
	"\x7b\x27\x61\x27\x3a\x30\x7d",
	"\x7b\x22\x69\x64\x22\x3a\x30\x2c\x7d",
	"\x7b\x22\x61\x22\x3a\x22\x62\x22\x7d\x2f\x2a\x2a\x2f",
	"\x7b\x22\x61\x22\x3a\x22\x62\x22\x7d\x2f\x2a\x2a\x2f\x2f",
	"\x7b\x22\x61\x22\x3a\x22\x62\x22\x7d\x2f\x2f",
	"\x7b\x22\x61\x22\x3a\x22\x62\x22\x7d\x2f",
	"\x7b\x22\x61\x22\x3a\x22\x62\x22\x2c\x2c\x22\x63\x22\x3a\x22\x64"
	"\x22\x7d",
	"\x7b\x61\x3a\x20\x22\x62\x22\x7d",
	"\x7b\x22\x61\x22\x3a\x22\x61",
	"\x7b\x20\x22\x66\x6f\x6f\x22\x20\x3a\x20\x22\x62\x61\x72\x22\x2c"
	"\x20\x22\x61\x22\x20\x7d",
	"\x7b\x22\x61\x22\x3a\x22\x62\x22\x7d\x23",
	"\x20",
	"\x5b\x22\x5c\x75\x44\x38\x30\x30\x5c\x22\x5d",
	"\x5b\x22\x5c\x75\x44\x38\x30\x30\x5c\x75\x22\x5d",
	"\x5b\x22\x5c\x75\x44\x38\x30\x30\x5c\x75\x31\x22\x5d",
	"\x5b\x22\x5c\x75\x44\x38\x30\x30\x5c\x75\x31\x78\x22\x5d",
	"\x5b\xc3\xa9\x5d",
	// SKIP: NUL in n_string_backslash_00.json
	"\x5b\x22\x5c\x78\x30\x30\x22\x5d",
	"\x5b\x22\x5c\x5c\x5c\x22\x5d",
	"\x5b\x22\x5c\x09\x22\x5d",
	"\x5b\x22\x5c\xf0\x9f\x8c\x80\x22\x5d",
	"\x5b\x22\x5c\x22\x5d",
	"\x5b\x22\x5c\x75\x30\x30\x41\x22\x5d",
	"\x5b\x22\x5c\x75\x44\x38\x33\x34\x5c\x75\x44\x64\x22\x5d",
	"\x5b\x22\x5c\x75\x44\x38\x30\x30\x5c\x75\x44\x38\x30\x30\x5c\x78"
	"\x22\x5d",
	"\x5b\x22\x5c\x75\xe5\x22\x5d",
	"\x5b\x22\x5c\x61\x22\x5d",
	"\x5b\x22\x5c\x75\x71\x71\x71\x71\x22\x5d",
	"\x5b\x22\x5c\xe5\x22\x5d",
	"\x5b\x5c\x75\x30\x30\x32\x30\x22\x61\x73\x64\x22\x5d",
	"\x5b\x5c\x6e\x5d",
	"\x22",
	"\x5b\x27\x73\x69\x6e\x67\x6c\x65\x20\x71\x75\x6f\x74\x65\x27\x5d"
	"",
	"\x61\x62\x63",
	"\x5b\x22\x5c",
	// SKIP: NUL in n_string_unescaped_ctrl_char.json
	"\x5b\x22\x6e\x65\x77\x0a\x6c\x69\x6e\x65\x22\x5d",
	"\x5b\x22\x09\x22\x5d",
	"\x22\x5c\x55\x41\x36\x36\x44\x22",
	"\x22\x22\x78",
	// SKIP: LONG  n_structure_100000_opening_arrays.json
	"\x5b\xe2\x81\xa0\x5d",
	"\xef\xbb\xbf",
	"\x3c\x2e\x3e",
	"\x5b\x3c\x6e\x75\x6c\x6c\x3e\x5d",
	"\x5b\x31\x5d\x78",
	"\x5b\x31\x5d\x5d",
	"\x5b\x22\x61\x73\x64\x5d",
	"\x61\xc3\xa5",
	"\x5b\x54\x72\x75\x65\x5d",
	"\x31\x5d",
	"\x7b\x22\x78\x22\x3a\x20\x74\x72\x75\x65\x2c",
	"\x5b\x5d\x5b\x5d",
	"\x5d",
	"\xef\xbb\x7b\x7d",
	"\xe5",
	"\x5b",
	"",
	// SKIP: NUL in n_structure_null-byte-outside-string.json
	"\x32\x40",
	"\x7b\x7d\x7d",
	"\x7b\x22\x22\x3a",
	"\x7b\x22\x61\x22\x3a\x2f\x2a\x63\x6f\x6d\x6d\x65\x6e\x74\x2a\x2f"
	"\x22\x62\x22\x7d",
	"\x7b\x22\x61\x22\x3a\x20\x74\x72\x75\x65\x7d\x20\x22\x78\x22",
	"\x5b\x27",
	"\x5b\x2c",
	// SKIP: LONG  n_structure_open_array_object.json
	"\x5b\x7b",
	"\x5b\x22\x61",
	"\x5b\x22\x61\x22",
	"\x7b",
	"\x7b\x5d",
	"\x7b\x2c",
	"\x7b\x5b",
	"\x7b\x22\x61",
	"\x7b\x27\x61\x27",
	"\x5b\x22\x5c\x7b\x5b\x22\x5c\x7b\x5b\x22\x5c\x7b\x5b\x22\x5c\x7b"
	"",
	"\xe9",
	"\x2a",
	"\x7b\x22\x61\x22\x3a\x22\x62\x22\x7d\x23\x7b\x7d",
	"\x5b\x5c\x75\x30\x30\x30\x41\x22\x22\x5d",
	"\x5b\x31",
	"\x5b\x20\x66\x61\x6c\x73\x65\x2c\x20\x6e\x75\x6c",
	"\x5b\x20\x74\x72\x75\x65\x2c\x20\x66\x61\x6c\x73",
	"\x5b\x20\x66\x61\x6c\x73\x65\x2c\x20\x74\x72\x75",
	"\x7b\x22\x61\x73\x64\x22\x3a\x22\x61\x73\x64\x22",
	"\xc3\xa5",
	"\x5b\xe2\x81\xa0\x5d",
	"\x5b\x0c\x5d",
	NULL
};

static void
test_good(const char *j)
{
	struct vjsn *js;
	const char *err;

	js = vjsn_parse(j, &err);
	if (js == NULL || err != NULL) {
		fprintf(stderr, "Parse error: %s\n%s\n", err, j);
		exit(1);
	}
	printf("GOOD: %s\n", j);
	vjsn_dump(js, stdout);
	vjsn_delete(&js);
}

static void
test_bad(const char *j)
{
	struct vjsn *js;
	const char *err;

	js = vjsn_parse(j, &err);
	if (js != NULL || err == NULL) {
		fprintf(stderr, "Parse succeeded %s\n", j);
		exit(1);
	}
	printf("BAD: %s %s\n", err, j);
}

int
main(int argc, char **argv)
{
	const char **s;

	(void)argc;
	(void)argv;
	for (s = good; *s != NULL; s++)
		test_good(*s);
	for (s = bad; *s != NULL; s++)
		test_bad(*s);

	/*
	 * This is part of Nicolas i(ndeterminate) test set, for reasons I
	 * do not fully grasp, but we want it to test bad.
	 */
	test_bad("\"\\uDFAA\"");
	printf("Tests done\n");
	return (0);
}

#endif
