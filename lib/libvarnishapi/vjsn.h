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

extern const char VJSN_OBJECT[];
extern const char VJSN_ARRAY[];
extern const char VJSN_NUMBER[];
extern const char VJSN_STRING[];
extern const char VJSN_TRUE[];
extern const char VJSN_FALSE[];
extern const char VJSN_NULL[];

struct vjsn_val {
	unsigned		magic;
#define VJSN_VAL_MAGIC		0x08a06b80
	const char		*type;
	const char		*name;
	VTAILQ_ENTRY(vjsn_val)	list;
	VTAILQ_HEAD(,vjsn_val)	children;
	char			*value;
};

struct vjsn {
	unsigned		magic;
#define VJSN_MAGIC		0x86a7f02b

	char			*raw;
	char			*ptr;
	struct vjsn_val		*value;
	const char		*err;
};

struct vjsn *vjsn_parse(const char *, const char **);
void vjsn_dump(const struct vjsn *js, FILE *fo);
struct vjsn_val *vjsn_child(const struct vjsn_val *, const char *);
