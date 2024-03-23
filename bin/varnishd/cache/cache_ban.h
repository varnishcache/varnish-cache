/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * Ban processing
 *
 * A ban consists of a number of conditions (or tests), all of which must be
 * satisfied.  Here are some potential bans we could support:
 *
 *	req.url == "/foo"
 *	req.url ~ ".iso" && obj.size > 10MB
 *	req.http.host ~ "web1.com" && obj.http.set-cookie ~ "USER=29293"
 *
 * We make the "&&" mandatory from the start, leaving the syntax space
 * for latter handling of "||" as well.
 *
 * Bans are compiled into bytestrings as follows:
 *	8 bytes	- double: timestamp		XXX: Byteorder ?
 *	4 bytes - be32: length
 *	1 byte - flags: 0x01: BAN_F_{REQ|OBJ|COMPLETED}
 *	N tests
 * A test have this form:
 *	1 byte - arg (see ban_vars.h col 3 "BANS_ARG_XXX")
 *	(n bytes) - http header name, canonical encoding
 *	lump - comparison arg
 *	1 byte - operation (BANS_OPER_)
 *	(lump) - compiled regexp
 * A lump is:
 *	4 bytes - be32: length
 *	n bytes - content
 *
 */

/*--------------------------------------------------------------------
 * BAN string defines & magic markers
 */

#define BANS_TIMESTAMP		0
#define BANS_LENGTH		8
#define BANS_FLAGS		12
#define BANS_HEAD_LEN		16

#define BANS_FLAG_REQ		(1<<0)
#define BANS_FLAG_OBJ		(1<<1)
#define BANS_FLAG_COMPLETED	(1<<2)
#define BANS_FLAG_HTTP		(1<<3)
#define BANS_FLAG_DURATION	(1<<4)
#define BANS_FLAG_NODEDUP	(1<<5)

#define BANS_OPER_EQ		0x10
#define BANS_OPER_OFF_		BANS_OPER_EQ
#define BANS_OPER_NEQ		0x11
#define BANS_OPER_MATCH		0x12
#define BANS_OPER_NMATCH	0x13
#define BANS_OPER_GT		0x14
#define BANS_OPER_GTE		0x15
#define BANS_OPER_LT		0x16
#define BANS_OPER_LTE		0x17
#define BANS_OPER_LIM_		(BANS_OPER_LTE + 1)

#define BAN_OPERIDX(x) ((x) - BANS_OPER_OFF_)
#define BAN_OPERARRSZ  (BANS_OPER_LIM_ - BANS_OPER_OFF_)
#define ASSERT_BAN_OPER(x) assert((x) >= BANS_OPER_OFF_ && (x) < BANS_OPER_LIM_)

#define BANS_ARG_URL		0x18
#define BANS_ARG_OFF_		BANS_ARG_URL
#define BANS_ARG_REQHTTP	0x19
#define BANS_ARG_OBJHTTP	0x1a
#define BANS_ARG_OBJSTATUS	0x1b
#define BANS_ARG_OBJTTL	0x1c
#define BANS_ARG_OBJAGE	0x1d
#define BANS_ARG_OBJGRACE	0x1e
#define BANS_ARG_OBJKEEP	0x1f
#define BANS_ARG_LIM		(BANS_ARG_OBJKEEP + 1)

#define BAN_ARGIDX(x) ((x) - BANS_ARG_OFF_)
#define BAN_ARGARRSZ  (BANS_ARG_LIM - BANS_ARG_OFF_)
#define ASSERT_BAN_ARG(x) assert((x) >= BANS_ARG_OFF_ && (x) < BANS_ARG_LIM)

// has an arg1_spec (BANS_FLAG_HTTP at build time)
#define BANS_HAS_ARG1_SPEC(arg)	\
	((arg) == BANS_ARG_REQHTTP ||	\
	 (arg) == BANS_ARG_OBJHTTP)

// has an arg2_spec (regex)
#define BANS_HAS_ARG2_SPEC(oper)	\
	((oper) == BANS_OPER_MATCH ||	\
	 (oper) == BANS_OPER_NMATCH)

// has an arg2_double (BANS_FLAG_DURATION at build time)
#define BANS_HAS_ARG2_DOUBLE(arg)	\
	((arg) >= BANS_ARG_OBJTTL &&	\
	 (arg) <= BANS_ARG_OBJKEEP)

/*--------------------------------------------------------------------*/

struct ban {
	unsigned		magic;
#define BAN_MAGIC		0x700b08ea
	unsigned		flags;		/* BANS_FLAG_* */
	VTAILQ_ENTRY(ban)	list;
	VTAILQ_ENTRY(ban)	l_list;
	int64_t			refcount;

	VTAILQ_HEAD(,objcore)	objcore;
	uint8_t			*spec;
};

VTAILQ_HEAD(banhead_s,ban);

bgthread_t ban_lurker;
extern struct lock ban_mtx;
extern int ban_shutdown;
extern struct banhead_s ban_head;
extern struct ban * volatile ban_start;
extern pthread_cond_t	ban_lurker_cond;
extern uint64_t bans_persisted_bytes;
extern uint64_t bans_persisted_fragmentation;
extern const char * const ban_oper[BAN_OPERARRSZ + 1];

void ban_mark_completed(struct ban *);
unsigned ban_len(const uint8_t *banspec);
void ban_info_new(const uint8_t *ban, unsigned len);
void ban_info_drop(const uint8_t *ban, unsigned len);

int ban_evaluate(struct worker *wrk, const uint8_t *bs, struct objcore *oc,
    const struct http *reqhttp, unsigned *tests);
vtim_real ban_time(const uint8_t *banspec);
struct ban * BAN_Alloc(struct ban_proto *bp, ssize_t *lnp);
unsigned BAN_Cancel(const uint8_t *, struct ban *ban);
const char * BAN_Error(struct ban_proto *bp);
void BAN_Free(struct ban *b);
void ban_kick_lurker(void);
