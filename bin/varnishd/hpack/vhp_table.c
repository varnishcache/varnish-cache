/*-
 * Copyright (c) 2016 Varnish Software
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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
 * Layout:
 *
 * buf [
 *    <x bytes name index n - 1> <x bytes value index n - 1>
 *    <x bytes name index n - 2> <x bytes value index n - 2>
 *    ...
 *    <x bytes name index 0> <x bytes value index 0>
 *
 *    (padding bytes for pointer alignment)
 *
 *    <struct vht_entry index 0>
 *    <struct vht_entry index 1>
 *    ...
 *    <struct vht_entry index n - 1>
 * ]
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#include "vdef.h"
#include "miniobj.h"
#include "vas.h"

#include "hpack/vhp.h"

#define VHT_STATIC_MAX 61

struct vht_static {
	const char *name;
	unsigned namelen;
	const char *value;
	unsigned valuelen;
};

static const struct vht_static static_table[] = {
#define HPS(NUM, NAME, VAL)			\
	{ NAME, sizeof NAME - 1, VAL, sizeof VAL - 1 },
#include "tbl/vhp_static.h"
};

#define TBLSIZE(tbl) ((tbl)->size + (tbl)->n * VHT_ENTRY_SIZE)
#define ENTRIES(buf, bufsize, n)					\
	(((struct vht_entry *)((uintptr_t)(buf) + bufsize)) - (n))
#define TBLENTRIES(tbl) ENTRIES((tbl)->buf, (tbl)->bufsize, (tbl)->n)
#define TBLENTRY(tbl, i) (&TBLENTRIES(tbl)[(i)])
#define ENTRYLEN(e) ((e)->namelen + (e)->valuelen)
#define ENTRYSIZE(e) (ENTRYLEN(e) + VHT_ENTRY_SIZE)

/****************************************************************************/
/* Internal interface */

static void
vht_newentry(struct vht_table *tbl)
{
	struct vht_entry *e;

	assert(tbl->maxsize - TBLSIZE(tbl) >= VHT_ENTRY_SIZE);
	tbl->n++;
	e = TBLENTRY(tbl, 0);
	INIT_OBJ(e, VHT_ENTRY_MAGIC);
	e->offset = tbl->size;
}

/* Trim elements from the end until the table size is less than max. */
static void
vht_trim(struct vht_table *tbl, ssize_t max)
{
	unsigned u, v;
	int i;
	struct vht_entry *e;

	CHECK_OBJ_NOTNULL(tbl, VHT_TABLE_MAGIC);

	if (max < 0)
		max = 0;
	if (TBLSIZE(tbl) <= max)
		return;

	u = v = 0;
	for (i = tbl->n - 1; i >= 0; i--) {
		e = TBLENTRY(tbl, i);
		CHECK_OBJ_NOTNULL(e, VHT_ENTRY_MAGIC);
		if (TBLSIZE(tbl) - (u + v * VHT_ENTRY_SIZE) > max) {
			/* Trim entry */
			assert(e->offset == u);
			u += ENTRYLEN(e);
			v++;
			e->magic = 0;
		} else {
			/* Fixup offset */
			assert(e->offset >= u);
			e->offset -= u;
		}
	}
	assert(v <= tbl->n);

	memmove(tbl->buf, tbl->buf + u, tbl->size - u);
	memmove(TBLENTRY(tbl, v), TBLENTRY(tbl, 0), (tbl->n - v) * sizeof *e);
	tbl->n -= v;
	tbl->size -= u;
}

/* Append len bytes from buf to entry 0 name. Asserts if no space. */
static void
vht_appendname(struct vht_table *tbl, const char *buf, size_t len)
{
	struct vht_entry *e;

	CHECK_OBJ_NOTNULL(tbl, VHT_TABLE_MAGIC);
	e = TBLENTRY(tbl, 0);
	CHECK_OBJ_NOTNULL(e, VHT_ENTRY_MAGIC);
	AZ(e->valuelen);	/* Name needs to be set before value */
	assert(TBLSIZE(tbl) + len <= tbl->maxsize);
	assert(e->offset + e->namelen == tbl->size);
	memcpy(tbl->buf + tbl->size, buf, len);
	e->namelen += len;
	tbl->size += len;
}

/* Append len bytes from buf to entry 0 value. Asserts if no space. */
static void
vht_appendvalue(struct vht_table *tbl, const char *buf, size_t len)
{
	struct vht_entry *e;

	CHECK_OBJ_NOTNULL(tbl, VHT_TABLE_MAGIC);
	e = TBLENTRY(tbl, 0);
	CHECK_OBJ_NOTNULL(e, VHT_ENTRY_MAGIC);
	assert(TBLSIZE(tbl) + len <= tbl->maxsize);
	assert(e->offset + e->namelen + e->valuelen == tbl->size);
	memcpy(tbl->buf + tbl->size, buf, len);
	e->valuelen += len;
	tbl->size += len;
}

/****************************************************************************/
/* Public interface */

void
VHT_NewEntry(struct vht_table *tbl)
{

	CHECK_OBJ_NOTNULL(tbl, VHT_TABLE_MAGIC);
	assert(tbl->maxsize <= tbl->protomax);
	vht_trim(tbl, tbl->maxsize - VHT_ENTRY_SIZE);
	if (tbl->maxsize - TBLSIZE(tbl) < VHT_ENTRY_SIZE) {
		/* Maxsize less than one entry */
		assert(tbl->maxsize < VHT_ENTRY_SIZE);
		return;
	}
	vht_newentry(tbl);
}

int
VHT_NewEntry_Indexed(struct vht_table *tbl, unsigned idx)
{
	struct vht_entry *e, *e2;
	unsigned l, l2, lentry, lname, u;
	uint8_t tmp[48];

	/* Referenced name insertion. This has to be done carefully
	   because the referenced name may be evicted as the result of the
	   insertion (RFC 7541 section 4.4). */

	assert(sizeof tmp >= VHT_ENTRY_SIZE);

	CHECK_OBJ_NOTNULL(tbl, VHT_TABLE_MAGIC);
	assert(tbl->maxsize <= tbl->protomax);

	if (idx == 0)
		return (-1);

	if (idx <= VHT_STATIC_MAX) {
		/* Static table reference */
		VHT_NewEntry(tbl);
		VHT_AppendName(tbl, static_table[idx - 1].name,
		    static_table[idx - 1].namelen);
		return (0);
	}
	idx -= VHT_STATIC_MAX + 1;

	if (idx >= tbl->n)
		return (-1);	/* No such index */
	assert(tbl->maxsize >= VHT_ENTRY_SIZE);

	e = TBLENTRY(tbl, idx);
	CHECK_OBJ_NOTNULL(e, VHT_ENTRY_MAGIC);

	/* Count how many elements we can safely evict to make space
	   without evicting the referenced entry. */
	l = 0;
	u = 0;
	while (tbl->n - 1 - u > idx &&
	    tbl->maxsize - TBLSIZE(tbl) + l < VHT_ENTRY_SIZE + e->namelen) {
		e2 = TBLENTRY(tbl, tbl->n - 1 - u);
		CHECK_OBJ_NOTNULL(e2, VHT_ENTRY_MAGIC);
		l += ENTRYSIZE(e2);
		u++;
	}
	vht_trim(tbl, TBLSIZE(tbl) - l);
	e += u;
	assert(e == TBLENTRY(tbl, idx));

	if (tbl->maxsize - TBLSIZE(tbl) >= VHT_ENTRY_SIZE + e->namelen) {
		/* New entry with name fits */
		vht_newentry(tbl);
		idx++;
		assert(e == TBLENTRY(tbl, idx));
		CHECK_OBJ_NOTNULL(e, VHT_ENTRY_MAGIC);
		vht_appendname(tbl, tbl->buf + e->offset, e->namelen);
		return (0);
	}

	/* The tricky case: The referenced name will be evicted as a
	   result of the insertion. Move the referenced element data to
	   the end of the buffer through a local buffer. */

	/* Remove the referenced element from the entry list */
	assert(idx == tbl->n - 1);
	assert(e->offset == 0);
	lname = e->namelen;
	lentry = ENTRYLEN(e);
	e->magic = 0;
	memmove(TBLENTRY(tbl, 1), TBLENTRY(tbl, 0), (tbl->n - 1) * sizeof *e);
	tbl->n--;

	/* Shift the referenced element last using a temporary buffer. */
	l = 0;
	while (l < lentry) {
		l2 = lentry - l;
		if (l2 > sizeof tmp)
			l2 = sizeof tmp;
		memcpy(tmp, tbl->buf, l2);
		memmove(tbl->buf, tbl->buf + l2, tbl->size - l2);
		memcpy(tbl->buf + tbl->size - l2, tmp, l2);
		l += l2;
	}
	assert(l == lentry);
	tbl->size -= lentry;

	/* Fix up the existing element offsets */
	for (u = 0; u < tbl->n; u++) {
		e = TBLENTRY(tbl, u);
		CHECK_OBJ_NOTNULL(e, VHT_ENTRY_MAGIC);
		assert(e->offset >= lentry);
		e->offset -= lentry;
		assert(e->offset + ENTRYLEN(e) <= tbl->size);
	}

	/* Insert the new entry with the name now present at the end of
	   the buffer. */
	assert(tbl->maxsize - TBLSIZE(tbl) >= VHT_ENTRY_SIZE + lname);
	tbl->n++;
	e = TBLENTRY(tbl, 0);
	INIT_OBJ(e, VHT_ENTRY_MAGIC);
	e->offset = tbl->size;
	e->namelen = lname;
	tbl->size += lname;

	return (0);
}

void
VHT_AppendName(struct vht_table *tbl, const char *buf, ssize_t len)
{

	CHECK_OBJ_NOTNULL(tbl, VHT_TABLE_MAGIC);
	assert(tbl->maxsize <= tbl->protomax);
	if (len == 0)
		return;
	AN(buf);
	if (len < 0)
		len = strlen(buf);
	vht_trim(tbl, tbl->maxsize - len);
	if (tbl->n == 0)
		/* Max size exceeded */
		return;
	vht_appendname(tbl, buf, len);
}

void
VHT_AppendValue(struct vht_table *tbl, const char *buf, ssize_t len)
{

	CHECK_OBJ_NOTNULL(tbl, VHT_TABLE_MAGIC);
	assert(tbl->maxsize <= tbl->protomax);
	if (len == 0)
		return;
	AN(buf);
	if (len < 0)
		len = strlen(buf);
	vht_trim(tbl, tbl->maxsize - len);
	if (tbl->n == 0)
		/* Max size exceeded */
		return;
	vht_appendvalue(tbl, buf, len);
}

int
VHT_SetMaxTableSize(struct vht_table *tbl, size_t maxsize)
{

	CHECK_OBJ_NOTNULL(tbl, VHT_TABLE_MAGIC);
	assert(tbl->maxsize <= tbl->protomax);
	if (maxsize > tbl->protomax)
		return (-1);
	vht_trim(tbl, maxsize);
	assert(TBLSIZE(tbl) <= maxsize);
	tbl->maxsize = maxsize;
	return (0);
}

int
VHT_SetProtoMax(struct vht_table *tbl, size_t protomax)
{
	size_t bufsize;
	char *buf;

	CHECK_OBJ_NOTNULL(tbl, VHT_TABLE_MAGIC);
	assert(protomax <= UINT_MAX);
	assert(tbl->maxsize <= tbl->protomax);

	if (protomax == tbl->protomax)
		return (0);

	if (tbl->maxsize > protomax)
		tbl->maxsize = protomax;
	vht_trim(tbl, tbl->maxsize);
	assert(TBLSIZE(tbl) <= tbl->maxsize);

	bufsize = PRNDUP(protomax);
	if (bufsize == tbl->bufsize) {
		tbl->protomax = protomax;
		return (0);
	}

	buf = malloc(bufsize);
	if (buf == NULL)
		return (-1);

	if (tbl->buf != NULL) {
		memcpy(buf, tbl->buf, tbl->size);
		memcpy(ENTRIES(buf, bufsize, tbl->n), TBLENTRIES(tbl),
		    sizeof (struct vht_entry) * tbl->n);
		free(tbl->buf);
	}
	tbl->buf = buf;
	tbl->bufsize = bufsize;
	tbl->protomax = protomax;
	return (0);
}

const char *
VHT_LookupName(const struct vht_table *tbl, unsigned idx, size_t *plen)
{
	struct vht_entry *e;

	AN(plen);
	*plen = 0;

	if (idx == 0) {
		return (NULL);
	}
	if (idx <= VHT_STATIC_MAX) {
		*plen = static_table[idx - 1].namelen;
		return (static_table[idx - 1].name);
	}

	if (tbl == NULL)
		return (NULL);
	CHECK_OBJ_NOTNULL(tbl, VHT_TABLE_MAGIC);

	idx -= VHT_STATIC_MAX + 1;
	if (idx >= tbl->n)
		return (NULL);

	e = TBLENTRY(tbl, idx);
	CHECK_OBJ_NOTNULL(e, VHT_ENTRY_MAGIC);
	assert(e->offset + e->namelen <= tbl->size);
	*plen = e->namelen;
	return (tbl->buf + e->offset);
}

const char *
VHT_LookupValue(const struct vht_table *tbl, unsigned idx, size_t *plen)
{
	struct vht_entry *e;

	AN(plen);
	*plen = 0;

	if (idx == 0) {
		return (NULL);
	}
	if (idx <= VHT_STATIC_MAX) {
		*plen = static_table[idx - 1].valuelen;
		return (static_table[idx - 1].value);
	}

	if (tbl == NULL)
		return (NULL);
	CHECK_OBJ_NOTNULL(tbl, VHT_TABLE_MAGIC);

	idx -= VHT_STATIC_MAX + 1;
	if (idx >= tbl->n)
		return (NULL);

	e = TBLENTRY(tbl, idx);
	CHECK_OBJ_NOTNULL(e, VHT_ENTRY_MAGIC);
	assert(e->offset + e->namelen + e->valuelen <= tbl->size);
	*plen = e->valuelen;
	return (tbl->buf + e->offset + e->namelen);
}

int
VHT_Init(struct vht_table *tbl, size_t protomax)
{
	int r;

	assert(sizeof (struct vht_entry) <= VHT_ENTRY_SIZE);

	AN(tbl);
	if (protomax > UINT_MAX)
		return (-1);
	INIT_OBJ(tbl, VHT_TABLE_MAGIC);
	r = VHT_SetProtoMax(tbl, protomax);
	if (r) {
		tbl->magic = 0;
		return (r);
	}
	tbl->maxsize = tbl->protomax;
	return (0);
}

void
VHT_Fini(struct vht_table *tbl)
{

	CHECK_OBJ_NOTNULL(tbl, VHT_TABLE_MAGIC);
	free(tbl->buf);
	memset(tbl, 0, sizeof *tbl);
}

/****************************************************************************/
/* Internal interface */

#ifdef TABLE_TEST_DRIVER

#define VHT_DYNAMIC (VHT_STATIC_MAX + 1)

static int verbose = 0;

static int
vht_matchtable(struct vht_table *tbl, ...)
{
	va_list ap;
	unsigned u;
	int r;
	const char *a, *b;
	const struct vht_entry *e;

	CHECK_OBJ_NOTNULL(tbl, VHT_TABLE_MAGIC);

	va_start(ap, tbl);
	r = 0;
	for (u = 0; u < tbl->n; u++) {
		a = NULL;
		b = NULL;
		if (!r) {
			a = va_arg(ap, const char *);
			if (a == NULL) {
				printf("Too many elements in table\n");
				r = -1;
			} else {
				b = va_arg(ap, const char *);
				AN(b);
			}
		}

		e = TBLENTRY(tbl, u);
		CHECK_OBJ_NOTNULL(e, VHT_ENTRY_MAGIC);

		if (a) {
			AN(b);
			if (e->namelen != strlen(a) ||
			    strncmp(a, tbl->buf + e->offset, e->namelen))
				r = -1;
			if (e->valuelen != strlen(b) ||
			    strncmp(b, tbl->buf + e->offset + e->namelen,
			     e->valuelen))
				r = -1;
		}

		if (verbose || r)
			printf("%2u: @%03u (\"%.*s\", \"%.*s\")",
			    u, e->offset, (int)e->namelen, tbl->buf + e->offset,
			    (int)e->valuelen, tbl->buf + e->offset +e->namelen);

		if (a && (verbose || r)) {
			AN(b);
			printf(" %s (\"%s\", \"%s\")", (r ? "!=" : "=="), a, b);
		}

		if (verbose || r)
			printf("\n");
	}
	if (!r) {
		a = va_arg(ap, const char *);
		if (a != NULL) {
			printf("Missing elements in table\n");
			r = -1;
		}
	}
	va_end(ap);

	if (verbose || r)
		printf("n=%d, size=%u, tblsz=%u, max=%u, pmax=%u, bufsz=%u\n",
		    tbl->n, tbl->size, TBLSIZE(tbl), tbl->maxsize,
		    tbl->protomax, tbl->bufsize);

	return (r);
}

static void
test_1(void)
{
	/* Static table */

	const char *p;
	size_t l;

	if (verbose)
		printf("Test 1:\n");

	/* 1: ':authority' -> '' */
	p = VHT_LookupName(NULL, 1, &l);
	assert(l == strlen(":authority"));
	AZ(strncmp(p, ":authority", strlen(":authority")));
	p = VHT_LookupValue(NULL, 1, &l);
	AN(p);
	AZ(l);

	/* 5: ':path' -> '/index.html' */
	p = VHT_LookupValue(NULL, 5, &l);
	assert(l == strlen("/index.html"));
	AZ(strncmp(p, "/index.html", strlen("/index.html")));

	/* 61: 'www-authenticate' -> '' */
	p = VHT_LookupName(NULL, 61, &l);
	assert(l == strlen("www-authenticate"));
	AZ(strncmp(p, "www-authenticate", strlen("www-authenticate")));
	p = VHT_LookupValue(NULL, 61, &l);
	AN(p);
	AZ(l);

	/* Test zero index */
	AZ(VHT_LookupName(NULL, 0, &l));
	AZ(l);
	AZ(VHT_LookupValue(NULL, 0, &l));
	AZ(l);

	printf("Test 1 finished successfully\n");
	if (verbose)
		printf("\n");
}

static void
test_2(void)
{
	/* Test filling and overflow */

	struct vht_table tbl[1];

	if (verbose)
		printf("Test 2:\n");

	AZ(VHT_Init(tbl, VHT_ENTRY_SIZE + 10));

	VHT_NewEntry(tbl);
	VHT_AppendName(tbl, "12345", -1);
	VHT_AppendValue(tbl, "abcde", -1);
	assert(TBLSIZE(tbl) == VHT_ENTRY_SIZE + 10);
	/* 0: '12345' -> 'abcde' */
	AZ(vht_matchtable(tbl, "12345", "abcde", NULL));

	VHT_AppendValue(tbl, "f", -1);
	AZ(vht_matchtable(tbl, NULL));

	VHT_NewEntry(tbl);
	AZ(vht_matchtable(tbl, "", "", NULL));

	VHT_Fini(tbl);
	AZ(tbl->buf);

	printf("Test 2 finished successfully\n");
	if (verbose)
		printf("\n");
}

static void
test_3(void)
{
	/* Test change in proto max size and dynamic max size */

	struct vht_table tbl[1];

	if (verbose)
		printf("Test 3:\n");

	AZ(VHT_Init(tbl, 4096));

	VHT_NewEntry(tbl);
	VHT_AppendName(tbl, "a", -1);
	VHT_AppendValue(tbl, "12345", -1);
	VHT_NewEntry(tbl);
	VHT_AppendName(tbl, "b", -1);
	VHT_AppendValue(tbl, "67890", -1);
	VHT_NewEntry(tbl);
	VHT_AppendName(tbl, "c", -1);
	VHT_AppendValue(tbl, "abcde", -1);
	AZ(vht_matchtable(tbl, "c", "abcde", "b", "67890", "a", "12345", NULL));

	/* Buffer reallocation */
	AZ(VHT_SetProtoMax(tbl, VHT_ENTRY_SIZE * 2 + 6 * 2));
	AZ(vht_matchtable(tbl, "c", "abcde", "b", "67890", NULL));

	/* Increase table size beyond protomax */
	assert(VHT_SetMaxTableSize(tbl, VHT_ENTRY_SIZE * 2 + 6 * 2 + 1) == -1);

	/* Decrease by one */
	AZ(VHT_SetMaxTableSize(tbl, VHT_ENTRY_SIZE * 2 + 6 * 2 - 1));
	AZ(vht_matchtable(tbl, "c", "abcde", NULL));

	/* Increase by one back to protomax */
	AZ(VHT_SetMaxTableSize(tbl, VHT_ENTRY_SIZE * 2 + 6 * 2));
	AZ(vht_matchtable(tbl, "c", "abcde", NULL));

	/* Add entry */
	VHT_NewEntry(tbl);
	VHT_AppendName(tbl, "d", -1);
	VHT_AppendValue(tbl, "ABCDE", -1);
	AZ(vht_matchtable(tbl, "d", "ABCDE", "c", "abcde", NULL));

	/* Set to zero */
	AZ(VHT_SetMaxTableSize(tbl, 0));
	AZ(vht_matchtable(tbl, NULL));
	VHT_NewEntry(tbl);
	AZ(vht_matchtable(tbl, NULL));

	/* Set protomax to zero */
	AZ(VHT_SetProtoMax(tbl, 0));
	AZ(vht_matchtable(tbl, NULL));
	VHT_NewEntry(tbl);
	AZ(vht_matchtable(tbl, NULL));

	VHT_Fini(tbl);

	printf("Test 3 finished successfully\n");
	if (verbose)
		printf("\n");
}

static void
test_4(void)
{
	/* Referenced name new entry */

	struct vht_table tbl[1];
	static const char longname[] =
	    "1234567890"
	    "1234567890"
	    "1234567890"
	    "1234567890"
	    "1234567890"
	    "1";	/* 51 bytes + VHT_ENTRY_SIZE == 83 */

	if (verbose)
		printf("Test 4:\n");

	AZ(VHT_Init(tbl, VHT_ENTRY_SIZE * 2 + 10 * 2)); /* 84 bytes */

	/* New entry indexed from static table */
	AZ(VHT_NewEntry_Indexed(tbl, 4));
	VHT_AppendValue(tbl, "12345", -1);
	AZ(vht_matchtable(tbl, ":path", "12345", NULL));

	/* New entry indexed from dynamic table */
	AZ(VHT_NewEntry_Indexed(tbl, VHT_DYNAMIC + 0));
	VHT_AppendValue(tbl, "abcde", -1);
	AZ(vht_matchtable(tbl, ":path", "abcde", ":path", "12345", NULL));
	AZ(tbl->maxsize - TBLSIZE(tbl)); /* No space left */

	/* New entry indexed from dynamic table, no overlap eviction */
	AZ(VHT_NewEntry_Indexed(tbl, VHT_DYNAMIC + 0));
	VHT_AppendValue(tbl, "ABCDE", -1);
	AZ(vht_matchtable(tbl, ":path", "ABCDE", ":path", "abcde", NULL));

	/* New entry indexed from dynamic table, overlap eviction */
	AZ(VHT_NewEntry_Indexed(tbl, VHT_DYNAMIC + 1));
	AZ(vht_matchtable(tbl, ":path", "", ":path", "ABCDE", NULL));

	/* New entry indexed from dynamic table, overlap eviction with
	   overlap larger than the copy buffer size */
	VHT_NewEntry(tbl);
	VHT_AppendName(tbl, longname, strlen(longname));
	AZ(vht_matchtable(tbl, longname, "", NULL));
	AZ(VHT_NewEntry_Indexed(tbl, VHT_DYNAMIC + 0));
	VHT_AppendValue(tbl, "2", -1);
	AZ(vht_matchtable(tbl, longname, "2", NULL));

	VHT_Fini(tbl);
	printf("Test 4 finished successfully\n");
	if (verbose)
		printf("\n");
}

static void
test_5(void)
{
	struct vht_table tbl[1];
	char buf_a[3];
	char buf_b[2];
	int i;

	if (verbose)
		printf("Test 5:\n");

	assert(sizeof buf_a > 0);
	for (i = 0; i < sizeof buf_a - 1; i++)
		buf_a[i] = 'a';
	buf_a[i++] = '\0';

	assert(sizeof buf_b > 0);
	for (i = 0; i < sizeof buf_b - 1; i++)
		buf_b[i] = 'b';
	buf_b[i++] = '\0';

	AZ(VHT_Init(tbl,
		3 * ((sizeof buf_a - 1)+(sizeof buf_b - 1)+VHT_ENTRY_SIZE)));

	VHT_NewEntry(tbl);
	VHT_AppendName(tbl, buf_a, sizeof buf_a - 1);
	VHT_AppendValue(tbl, buf_b, sizeof buf_b - 1);
	AZ(vht_matchtable(tbl, buf_a, buf_b, NULL));

	VHT_NewEntry(tbl);
	VHT_AppendName(tbl, buf_a, sizeof buf_a - 1);
	VHT_AppendValue(tbl, buf_b, sizeof buf_b - 1);
	AZ(vht_matchtable(tbl, buf_a, buf_b, buf_a, buf_b, NULL));

	VHT_NewEntry(tbl);
	VHT_AppendName(tbl, buf_a, sizeof buf_a - 1);
	VHT_AppendValue(tbl, buf_b, sizeof buf_b - 1);
	AZ(vht_matchtable(tbl, buf_a, buf_b, buf_a, buf_b, buf_a, buf_b, NULL));

	AZ(VHT_NewEntry_Indexed(tbl, VHT_DYNAMIC + 2));
	VHT_AppendValue(tbl, buf_b, sizeof buf_b - 1);
	AZ(vht_matchtable(tbl, buf_a, buf_b, buf_a, buf_b, buf_a, buf_b, NULL));

	VHT_Fini(tbl);
	printf("Test 5 finished successfully\n");
	if (verbose)
		printf("\n");
}

int
main(int argc, char **argv)
{

	if (argc == 2 && !strcmp(argv[1], "-v"))
		verbose = 1;
	else if (argc != 1) {
		fprintf(stderr, "Usage: %s [-v]\n", argv[0]);
		return (1);
	}

	if (verbose) {
		printf("sizeof (struct vht_table) == %zu\n",
		    sizeof (struct vht_table));
		printf("sizeof (struct vht_entry) == %zu\n",
		    sizeof (struct vht_entry));
		printf("\n");
	}

	test_1();
	test_2();
	test_3();
	test_4();
	test_5();

	return (0);
}

#endif	/* TABLE_TEST_DRIVER */
