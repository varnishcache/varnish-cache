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
 * $Id$
 *
 */

struct ban_test;

/* A ban-testing function */
typedef int ban_cond_f(const struct ban_test *bt, const struct object *o, const struct sess *sp);

/* Each individual test to be performed on a ban */
struct ban_test {
	unsigned		magic;
#define BAN_TEST_MAGIC		0x54feec67
	VTAILQ_ENTRY(ban_test)	list;
	ban_cond_f		*func;
	int			flags;
#define BAN_T_REGEXP		(1 << 0)
#define BAN_T_NOT		(1 << 1)
	regex_t			re;
	char			*dst;
	char			*src;
};

struct ban {
	unsigned		magic;
#define BAN_MAGIC		0x700b08ea
	VTAILQ_ENTRY(ban)	list;
	unsigned		refcount;
	int			flags;
#define BAN_F_GONE		(1 << 0)
#define BAN_F_PENDING		(1 << 1)
	VTAILQ_HEAD(,ban_test)	tests;
	double			t0;
	struct vsb		*vsb;
	char			*test;
};
